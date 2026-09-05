#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspnet.h>
#include <pspnet_apctl.h>
#include <pspnet_inet.h>
#include <pspdisplay.h>
#include <pspjpeg.h>
#include <pspaudio.h>
#include <pspaudiocodec.h>
#include <psppower.h>
#include <psputils.h>
#include <psputility.h>
#include <psputility_netmodules.h>
#include <psputility_netparam.h>
#include <psputility_modules.h>
#include <psputility_avmodules.h>
#include <pspiofilemgr.h>
#include <kubridge.h>

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "config.h"
#include "h264_decoder.h"
#include "h264_hw.h"

PSP_MODULE_INFO("PSPStreamer", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define RESPONSE_SIZE (512 * 1024)
/* The PSP-3000 has 64 MiB RAM.  Keep a generously sized, paged directory
 * index so large anime/series roots are browsable instead of silently cut at
 * 24 entries. */
#define MAX_ITEMS 1024
#define LIST_ROWS 20
#define JPEG_BUFFER_BYTES (512 * 1024)
#define H264_BUFFER_BYTES (768 * 1024)
#define VIDEO_WIDTH 480
#define VIDEO_HEIGHT 272
#define VIDEO_STRIDE 512
#define TITLE_SIZE 128
/* IDs encode the complete relative path and can be long for episode files. */
#define ID_SIZE 512
#define SCE_ERROR_LIBRARY_ALREADY_EXISTS ((int)0x8002013B)

typedef struct {
    char title[TITLE_SIZE];
    char value[ID_SIZE];
    int is_folder;
} LibraryItem;

typedef struct {
    int number;
    char language[16];
} StreamTrack;

/* Subtitle times are server-normalised to the same 20.1-fps presentation
 * clock as the raw H.264 stream.  Keeping them in frames avoids a second,
 * drifting millisecond clock on the PSP. */
#define MAX_SUBTITLE_CUES 960
#define SUBTITLE_TEXT_SIZE 160
#define SUBTITLE_FONT_CELL_WIDTH 16
#define SUBTITLE_FONT_CELL_HEIGHT 20
#define SUBTITLE_FONT_BYTES (16 * 16 * SUBTITLE_FONT_CELL_WIDTH * SUBTITLE_FONT_CELL_HEIGHT)
typedef struct {
    int start_frame;
    int end_frame;
    char text[SUBTITLE_TEXT_SIZE];
} SubtitleCue;

static char response[RESPONSE_SIZE];
/* 512 pixels is the required power-of-two display stride. */
static unsigned char jpeg_buffer[JPEG_BUFFER_BYTES] __attribute__((aligned(64)));
static unsigned char h264_buffer[H264_BUFFER_BYTES] __attribute__((aligned(64)));
/* sceJpeg writes tightly packed RGBA rows. The LCD, however, reads a 512-pixel
 * stride from VRAM. Keep those layouts separate and copy line by line. */
static unsigned char decoded_frame[VIDEO_WIDTH * VIDEO_HEIGHT * 4] __attribute__((aligned(64)));
static LibraryItem items[MAX_ITEMS];
static int item_count;
static char current_path[ID_SIZE];
static int network_ready;
static int http_ready;
static int active_network_profile;
static const char *failure_step = "Network";
static const char *video_step = "Start";
static volatile int audio_running;
static volatile int audio_start;
static volatile int audio_clock_started;
static volatile int audio_socket_fd = -1;
static volatile int audio_output_thread_id = -1;
/* Kept deliberately numeric: it is displayed after START exits playback and
 * identifies the exact network/audio stage on real hardware. */
static volatile int audio_state;
static int hardware_decoder_ready;
static int video_modules_ready;
static int hardware_decoder_frames;
static int hardware_runtime_result = -9999;
static int performance_result = -9999;
static const char *hardware_runtime_step = "not loaded";
/* This is intentionally shown on screen on a load failure: on real PSPs the
 * GAME folder name is user-defined, so it is the fastest way to diagnose a
 * PRX copied beside a different EBOOT. */
static char hardware_runtime_path[256] = "-";

static int load_hardware_avc_runtime(void) {
    char bridge_path[256], cwd[192];
    const char *fallback_dirs[] = {
        PSP_STREAMER_INSTALL_DIR,
        "ms0:/PSP/GAME/PSPSTREAMER",
        "ms0:/PSP/GAME/PSP_STREAMER",
        "ms0:/PSP/GAME/PSP Streamer"
    };
    int i;
    SceIoStat stat;
    SceUID module_id;
    int status = 0, result;
    /* GAME folders are user-chosen (and may differ in case/spelling).  The
     * current directory is the EBOOT's own folder, so the bridge always
     * travels next to EBOOT.PBP rather than relying on a hard-coded path. */
    if (!getcwd(cwd, sizeof(cwd))) {
        strncpy(cwd, PSP_STREAMER_INSTALL_DIR, sizeof(cwd) - 1);
        cwd[sizeof(cwd) - 1] = '\0';
    }
    snprintf(bridge_path, sizeof(bridge_path), "%s/cooleyesBridge.prx", cwd);
    strncpy(hardware_runtime_path, bridge_path, sizeof(hardware_runtime_path) - 1);
    hardware_runtime_path[sizeof(hardware_runtime_path) - 1] = '\0';
    /* ARK's kuKernelLoadModule performs the privileged load while this
     * EBOOT remains user-mode.  The bridge exports the ABI mpeg_vsh needs. */
    hardware_runtime_step = "Loading bridge";
    module_id = kuKernelLoadModule(bridge_path, 0, NULL);
    /* Some launchers leave cwd at ms0:/ instead of the GAME directory.  Only
     * retry when the primary path genuinely does not exist; a real PRX load
     * error must remain visible to the user. */
    if (module_id == (SceUID)0x80010002) {
        for (i = 0; i < (int)(sizeof(fallback_dirs) / sizeof(fallback_dirs[0])); i++) {
            snprintf(bridge_path, sizeof(bridge_path), "%s/cooleyesBridge.prx", fallback_dirs[i]);
            if (sceIoGetstat(bridge_path, &stat) < 0) continue;
            strncpy(hardware_runtime_path, bridge_path, sizeof(hardware_runtime_path) - 1);
            hardware_runtime_path[sizeof(hardware_runtime_path) - 1] = '\0';
            module_id = kuKernelLoadModule(bridge_path, 0, NULL);
            break;
        }
    }
    if (module_id < 0) return module_id;
    hardware_runtime_step = "Starting bridge";
    result = sceKernelStartModule(module_id, 0, NULL, &status, NULL);
    if (result < 0) return result;
    /* mpeg_vsh is not standalone: its imports are supplied by the official
     * AV codec module.  Loading it through the user-mode utility is supported
     * on 6.61 and avoids the missing-library error from sceKernelStartModule. */
    hardware_runtime_step = "Loading AVCodec";
    result = sceUtilityLoadAvModule(PSP_AV_MODULE_AVCODEC);
    if (result < 0) return result;
    /* This is the user's own 6.61 firmware module, not a bundled PRX. */
    hardware_runtime_step = "Loading mpeg_vsh";
    module_id = kuKernelLoadModule("flash0:/kd/mpeg_vsh.prx", 0, NULL);
    if (module_id < 0) return module_id;
    hardware_runtime_step = "Starting mpeg_vsh";
    result = sceKernelStartModule(module_id, 0, NULL, &status, NULL);
    if (result < 0) return result;
    /* A positive module UID from StartModule is also successful. */
    hardware_runtime_step = "ready";
    return 0;
}
static char audio_media_id[ID_SIZE];
/* Keep the DMA buffer out of the audio thread's stack.  Its HTTP header and
 * request already consume roughly 6 KiB; on a 16-KiB thread that left too
 * little headroom once the socket and libc routines were active. */
#define MP3_DECODE_SAMPLES 1152
#define MP3_FRAMES_PER_AUDIO_BLOCK 4
#define AUDIO_BLOCK_SAMPLES (MP3_DECODE_SAMPLES * MP3_FRAMES_PER_AUDIO_BLOCK)
/* Two 104-ms blocks retain the previous ~0.2 s runway while reducing DAC
 * hand-offs by a quarter; those hand-offs were the remaining faint ticks. */
#define AUDIO_PREFILL_BLOCKS 2
#define AUDIO_QUEUE_BLOCKS 8
#define MP3_INPUT_BUFFER_BYTES 4096
#define MP3_MAX_FRAME_BYTES 576
/* Producer/consumer queue: network jitter is absorbed here while the output
 * thread feeds the DSP on time. */
static short audio_samples[AUDIO_BLOCK_SAMPLES * 2 * AUDIO_QUEUE_BLOCKS] __attribute__((aligned(64)));
/* Direct firmware codec input and its required work area.  Unlike sceMp3,
 * this path has no fake file offsets or opaque streaming ring. */
static unsigned char mp3_input_buffer[MP3_INPUT_BUFFER_BYTES] __attribute__((aligned(64)));
static unsigned long mp3_codec[65] __attribute__((aligned(64)));
static void *mp3_codec_work;
static volatile int audio_queue_read, audio_queue_write, audio_queue_count;
static char status[128] = "Starting network ...";
static int selected_audio_track;
static int selected_subtitle_track = -1;
static int selected_audio_quality = 2;
static StreamTrack audio_tracks[8], subtitle_tracks[8];
static int audio_track_count, subtitle_track_count;
static SubtitleCue *subtitle_cues;
static int subtitle_cue_count;
static unsigned char *subtitle_font;
static int subtitle_client_side;
static float current_duration_seconds;
static int playback_reached_end;
static int stream_start_seconds;
static int resume_pending;
static char resume_media_id[ID_SIZE];
static int seek_requested;
#define SETTINGS_PATH "ms0:/PSP/SYSTEM/PSPStreamer.cfg"
static char server_host[64] = PSP_STREAMER_HOST;
static int server_port = PSP_STREAMER_PORT;
static struct in_addr cached_server_address;
static int have_cached_server_address;

static const char *audio_quality_name(void) {
    static const char *names[] = {"96k", "128k", "160k"};
    return names[selected_audio_quality];
}

static void load_playback_settings(void) {
    SceUID file = sceIoOpen(SETTINGS_PATH, PSP_O_RDONLY, 0);
    char data[192], *line;
    int count;
    if (file < 0) return;
    count = sceIoRead(file, data, sizeof(data) - 1);
    sceIoClose(file);
    if (count <= 0) return;
    data[count] = '\0';
    /* Accept the three-number format written by older builds too. */
    if (sscanf(data, "%d %d %d", &selected_audio_track, &selected_subtitle_track, &selected_audio_quality) != 3) {
        for (line = strtok(data, "\r\n"); line; line = strtok(NULL, "\r\n")) {
            if (!strncmp(line, "server=", 7) && line[7]) {
                const char *host = line + 7;
                if (!strncmp(host, "http://", 7)) host += 7;
                strncpy(server_host, host, sizeof(server_host) - 1);
                server_host[sizeof(server_host) - 1] = '\0';
                /* A URL path is never part of a TCP host name. */
                { char *slash = strchr(server_host, '/'); if (slash) *slash = '\0'; }
                /* Permit the convenient server=http://host:8091 spelling
                 * in addition to the separate port= line. */
                {
                    char *colon = strrchr(server_host, ':');
                    if (colon && colon[1]) {
                        int url_port = atoi(colon + 1);
                        if (url_port > 0 && url_port <= 65535) {
                            *colon = '\0';
                            server_port = url_port;
                        }
                    }
                }
            } else if (!strncmp(line, "port=", 5)) server_port = atoi(line + 5);
            else if (!strncmp(line, "audio=", 6)) selected_audio_track = atoi(line + 6);
            else if (!strncmp(line, "subtitle=", 9)) selected_subtitle_track = atoi(line + 9);
            else if (!strncmp(line, "quality=", 8)) selected_audio_quality = atoi(line + 8);
        }
    }
    if (selected_audio_track < 0 || selected_audio_track > 7) selected_audio_track = 0;
    if (selected_subtitle_track < -1 || selected_subtitle_track > 7) selected_subtitle_track = -1;
    if (selected_audio_quality < 0 || selected_audio_quality > 2) selected_audio_quality = 2;
    if (server_port < 1 || server_port > 65535) server_port = PSP_STREAMER_PORT;
    if (!server_host[0]) strcpy(server_host, PSP_STREAMER_HOST);
}

static void save_playback_settings(void) {
    SceUID file;
    char data[192];
    int length = snprintf(data, sizeof(data), "server=%s\nport=%d\naudio=%d\nsubtitle=%d\nquality=%d\n",
                          server_host, server_port, selected_audio_track, selected_subtitle_track, selected_audio_quality);
    file = sceIoOpen(SETTINGS_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (file >= 0) { sceIoWrite(file, data, length); sceIoClose(file); }
}

static int resolve_server_address(struct in_addr *address) {
    struct hostent *entry;
    if (inet_aton(server_host, address)) {
        cached_server_address = *address;
        have_cached_server_address = 1;
        return 0;
    }
    entry = gethostbyname(server_host);
    if (!entry || entry->h_length != 4 || !entry->h_addr_list || !entry->h_addr_list[0]) {
        if (have_cached_server_address) { *address = cached_server_address; return 0; }
        return -1;
    }
    memcpy(address, entry->h_addr_list[0], 4);
    cached_server_address = *address;
    have_cached_server_address = 1;
    return 0;
}

static int prepare_server(struct sockaddr_in *server) {
    memset(server, 0, sizeof(*server));
    server->sin_family = AF_INET;
    server->sin_port = htons((unsigned short)server_port);
    return resolve_server_address(&server->sin_addr);
}

/* Used only after HTTP headers arrived.  A timeout is not an error: it lets
 * the playback owner stop an audio worker after WLAN disappears. */
static int stream_recv(int socket_fd, void *buffer, int length, int timeout_ms) {
    struct SceNetInetPollfd pollfd = { socket_fd, SCE_NET_INET_POLLIN, 0 };
    int ready = sceNetInetPoll(&pollfd, 1, timeout_ms);
    if (ready == 0) return -2;
    if (ready < 0 || (pollfd.revents & (SCE_NET_INET_POLLERR | SCE_NET_INET_POLLHUP | SCE_NET_INET_POLLNVAL))) return 0;
    return (int)sceNetInetRecv(socket_fd, buffer, length, 0);
}

/* Never let a lost hotspot leave the UI or playback thread in a permanent
 * blocking recv().  Callers treat a timeout exactly like a dropped stream. */

static void keep_awake(void) {
    static unsigned long long last_tick;
    unsigned long long now = sceKernelGetSystemTimeWide();
    if (now - last_tick >= 30000000ULL) {
        scePowerTick(PSP_POWER_TICK_ALL);
        last_tick = now;
    }
}

int exit_callback(int arg1, int arg2, void *common) {
    (void)arg1; (void)arg2; (void)common;
    sceKernelExitGame();
    return 0;
}

int callback_thread(SceSize args, void *argp) {
    int callback_id;
    (void)args; (void)argp;
    callback_id = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(callback_id);
    sceKernelSleepThreadCB();
    return 0;
}

void setup_callbacks(void) {
    int thread_id = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, NULL);
    if (thread_id >= 0) sceKernelStartThread(thread_id, 0, NULL);
}

static int wait_for_network(void) {
    int state = 0;
    int elapsed;
    int result;
    int profile;
    failure_step = "Network Common";
    result = sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
    if (result < 0) return result;
    failure_step = "Network INET";
    result = sceUtilityLoadNetModule(PSP_NET_MODULE_INET);
    if (result < 0) return result;
    failure_step = "sceNetInit";
    result = sceNetInit(128 * 1024, 42, 0, 42, 0);
    if (result < 0) return result;
    failure_step = "sceNetInetInit";
    result = sceNetInetInit();
    if (result < 0) return result;
    failure_step = "sceNetApctlInit";
    result = sceNetApctlInit(0x1800, 48);
    if (result < 0) return result;
    sceNetApctlGetState(&state);
    if (state == PSP_NET_APCTL_STATE_GOT_IP) return 0;
    profile = PSP_NETWORK_PROFILE;
    if (profile == 0) {
        for (profile = 1; profile <= 100; profile++) {
            if (sceUtilityCheckNetParam(profile) == 0) break;
        }
        if (profile > 100) return -4;
    }
    active_network_profile = profile;
    failure_step = "Wi-Fi connection";
    result = sceNetApctlConnect(profile);
    if (result < 0) return result;
    for (elapsed = 0; elapsed < 150; elapsed++) {
        sceNetApctlGetState(&state);
        if (state == PSP_NET_APCTL_STATE_GOT_IP) return 0;
        sceKernelDelayThread(100000);
    }
    return -3;
}

static int wait_for_network_restore(void) {
    int state = 0, elapsed;
    for (elapsed = 0; elapsed < 40; elapsed++) {
        sceNetApctlGetState(&state);
        if (state == PSP_NET_APCTL_STATE_GOT_IP) return 0;
        if (state == 0 && active_network_profile > 0) sceNetApctlConnect(active_network_profile);
        sceKernelDelayThread(500000);
    }
    return -1;
}

static int http_get(const char *path, char *buffer, int buffer_size) {
    struct sockaddr_in server;
    char request[2048], *body;
    int socket_fd, received = 0, read_size, content_length = -1, header_length = -1;
    socket_fd = sceNetInetSocket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) return socket_fd;
    if (prepare_server(&server) < 0) { sceNetInetClose(socket_fd); return -1004; }
    if (sceNetInetConnect(socket_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        int error = sceNetInetGetErrno();
        sceNetInetClose(socket_fd);
        return error ? -error : -1001;
    }
    snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, server_host);
    if ((int)sceNetInetSend(socket_fd, request, strlen(request), 0) < 0) { sceNetInetClose(socket_fd); return -1002; }
    while (received < buffer_size - 1) {
        read_size = (int)sceNetInetRecv(socket_fd, buffer + received, buffer_size - 1 - received, 0);
        if (read_size <= 0) break;
        received += read_size;
        buffer[received] = '\0';
        /* The server supplies Content-Length for library/metadata replies.
         * Do not wait for TCP close: on a flaky access point that close can
         * arrive much later than the complete JSON response. */
        if (header_length < 0 && (body = strstr(buffer, "\r\n\r\n"))) {
            char *length_header = strstr(buffer, "Content-Length:");
            header_length = (int)(body + 4 - buffer);
            if (length_header) content_length = atoi(length_header + 15);
        }
        if (header_length >= 0 && content_length >= 0 &&
            received >= header_length + content_length) break;
    }
    sceNetInetClose(socket_fd);
    buffer[received] = '\0';
    body = strstr(buffer, "\r\n\r\n");
    if (!body || strncmp(buffer, "HTTP/1.", 7) || !strstr(buffer, " 200 ")) return -1003;
    body += 4;
    memmove(buffer, body, (size_t)(buffer + received - body + 1));
    return (int)strlen(buffer);
}

/* The subtitle endpoint deliberately emits a restricted JSON form:
 * {"t":"text","c":[[start,end,"ASCII text"],...]}.  Text has already
 * been normalised by the server, so a narrow parser is both safer and much
 * smaller than adding a general JSON library to the playback binary. */
static int prepare_client_subtitles(const char *media_id) {
    char path[ID_SIZE + 64];
    int result;
    subtitle_cue_count = 0;
    subtitle_client_side = 0;
    if (subtitle_cues) { free(subtitle_cues); subtitle_cues = NULL; }
    if (selected_subtitle_track < 0) return 0;
    snprintf(path, sizeof(path), "/api/subtitles/%s?track=%d", media_id, selected_subtitle_track);
    result = http_get(path, response, sizeof(response));
    if (result < 0) return result;
    /* Bitmap tracks keep the existing server-overlay fallback until the
     * sprite transport is available.  Never silently lose a requested PGS. */
    if (!strstr(response, "\"t\":\"text\"")) return 0;
    subtitle_client_side = 1;
    return 0;
}

/* Do not reserve the cue table in the EBOOT's permanent BSS.  sceMpegInit
 * requires a substantial contiguous allocation on 6.61; allocating text
 * data only after AVC is live preserves the previously proven start budget. */
static void subtitle_parse_prepared_response(void) {
    char *cursor;
    if (!subtitle_client_side || subtitle_cues) return;
    cursor = strstr(response, "\"c\":[");
    if (!cursor) { subtitle_client_side = 0; return; }
    cursor = strchr(cursor, '[');
    if (!cursor) { subtitle_client_side = 0; return; }
    subtitle_cues = memalign(64, MAX_SUBTITLE_CUES * sizeof(*subtitle_cues));
    if (!subtitle_cues) { subtitle_client_side = 0; return; }
    cursor++;
    while (subtitle_cue_count < MAX_SUBTITLE_CUES) {
        SubtitleCue *cue = &subtitle_cues[subtitle_cue_count];
        int consumed = 0;
        char *entry = strchr(cursor, '[');
        if (!entry || !strchr(cursor, ']')) break;
        if (sscanf(entry, "[%d,%d,\"%159[^\"]\"]%n", &cue->start_frame,
                   &cue->end_frame, cue->text, &consumed) != 3 || consumed <= 0) break;
        if (cue->end_frame > cue->start_frame) subtitle_cue_count++;
        cursor = entry + consumed;
    }
}

static void subtitle_release(void) {
    if (subtitle_cues) free(subtitle_cues);
    if (subtitle_font) free(subtitle_font);
    subtitle_cues = NULL;
    subtitle_font = NULL;
    subtitle_cue_count = 0;
}

static void subtitle_load_font(void) {
    char path[256], cwd[192];
    SceUID file;
    if (subtitle_font) return;
    if (!getcwd(cwd, sizeof(cwd))) return;
    snprintf(path, sizeof(path), "%s/subtitle_font.raw", cwd);
    file = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (file < 0) return;
    subtitle_font = memalign(64, SUBTITLE_FONT_BYTES);
    if (!subtitle_font || sceIoRead(file, subtitle_font, SUBTITLE_FONT_BYTES) != SUBTITLE_FONT_BYTES) {
        if (subtitle_font) free(subtitle_font);
        subtitle_font = NULL;
    }
    sceIoClose(file);
}

static int subtitle_utf8_char(const char **text) {
    const unsigned char *source = (const unsigned char *)*text;
    int value;
    if (source[0] < 0x80) { (*text)++; return source[0]; }
    if (source[0] == 0xc2 && source[1]) { value = source[1]; *text += 2; return value; }
    if (source[0] == 0xc3 && source[1]) { value = 0xc0 + (source[1] & 0x3f); *text += 2; return value; }
    (*text)++;
    return '?';
}

static void subtitle_draw_glyph(u32 *vram, int glyph, int left, int top) {
    /* The atlas is 16 glyphs wide.  Cells are not contiguous in memory: a
     * glyph's next scanline starts one complete 256-pixel atlas row later. */
    const unsigned char *bitmap = subtitle_font +
        (glyph >> 4) * SUBTITLE_FONT_CELL_HEIGHT * (SUBTITLE_FONT_CELL_WIDTH * 16) +
        (glyph & 15) * SUBTITLE_FONT_CELL_WIDTH;
    int x, y, dx, dy, pass;
    /* Drawing the outline and fill per pixel made a later outline erase an
     * already-drawn neighbouring white pixel.  Complete the dark pass first,
     * then paint the glyph face in a separate pass. */
    for (pass = 0; pass < 2; pass++) for (y = 0; y < SUBTITLE_FONT_CELL_HEIGHT; y++) for (x = 0; x < SUBTITLE_FONT_CELL_WIDTH; x++) {
        if (bitmap[y * SUBTITLE_FONT_CELL_WIDTH * 16 + x] > (pass ? 120 : 72)) {
            int px = left + x, py = top + y;
            if (!pass) {
                for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++)
                    if ((dx || dy) && px + dx >= 0 && px + dx < VIDEO_WIDTH && py + dy >= 0 && py + dy < VIDEO_HEIGHT)
                        vram[(py + dy) * VIDEO_STRIDE + px + dx] = 0x00000000;
            } else {
                vram[py * VIDEO_STRIDE + px] = 0x00ffffff;
            }
        }
    }
}

static const char *subtitle_draw_line(const char *text, int y, u32 *vram) {
    const char *cursor = text, *end = text, *last_space = NULL;
    int count = 0, glyph, index, left;
    while (*end && *end != '|' && count < 42) {
        const char *before = end;
        glyph = subtitle_utf8_char(&end);
        if (glyph == ' ') last_space = before;
        count++;
    }
    if (*end && *end != '|' && last_space) end = last_space;
    left = (VIDEO_WIDTH - count * 11) / 2;
    if (left < 4) left = 4;
    for (index = 0; cursor < end; index++) {
        glyph = subtitle_utf8_char(&cursor);
        subtitle_draw_glyph(vram, glyph, left + index * 11, y);
    }
    while (*end == ' ') end++;
    if (*end == '|') end++;
    return end;
}

static void subtitle_present(int absolute_frame) {
    int index = -1, cue_index, y;
    u32 *vram;
    if (!subtitle_client_side || !subtitle_cues) return;
    for (cue_index = 0; cue_index < subtitle_cue_count; cue_index++) {
        if (absolute_frame >= subtitle_cues[cue_index].start_frame &&
            absolute_frame < subtitle_cues[cue_index].end_frame) { index = cue_index; break; }
        if (subtitle_cues[cue_index].start_frame > absolute_frame) break;
    }
    if (index < 0) return;
    vram = (u32 *)0x44000000;
    if (!subtitle_font) subtitle_load_font();
    if (!subtitle_font) return; /* Never risk AVC for an optional font asset. */
    {
        const char *text = subtitle_cues[index].text;
        for (y = 0; *text && y < 3; y++) {
            const char *next = subtitle_draw_line(text, 210 + y * 20, vram);
            if (next == text) break;
            text = next;
        }
    }
}

/* The JPEG entry points are visible in user mode, but their AV backend is
 * normally not resident for a homebrew game. Use the firmware Utility API;
 * direct flash-PRX loading is not permitted in user mode. */
static int load_video_modules(void) {
    int result;
    /* Firmware AV modules remain resident for this application session.
     * Re-requesting MPEGBASE on some ARK/6.61 combinations returns 800200D9
     * even though the already-loaded module is usable. */
    if (video_modules_ready) return 0;
    video_step = "AVCODEC module";
    result = sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);
    if (result < 0 && result != (int)SCE_ERROR_MODULE_ALREADY_LOADED && result != SCE_ERROR_LIBRARY_ALREADY_EXISTS) return result;
    video_step = "MPEGBASE module";
    result = sceUtilityLoadModule(PSP_MODULE_AV_MPEGBASE);
    if (result < 0 && result != (int)SCE_ERROR_MODULE_ALREADY_LOADED && result != SCE_ERROR_LIBRARY_ALREADY_EXISTS) return result;
    video_modules_ready = 1;
    return 0;
}

static void present_decoded_frame(void) {
    unsigned char *vram = (unsigned char *)0x44000000; /* uncached VRAM alias */
    int row;
    for (row = 0; row < VIDEO_HEIGHT; row++) {
        memcpy(vram + row * VIDEO_STRIDE * 4,
               decoded_frame + row * VIDEO_WIDTH * 4,
               VIDEO_WIDTH * 4);
    }
    sceDisplaySetFrameBuf((void *)0x04000000, VIDEO_STRIDE, PSP_DISPLAY_PIXEL_FORMAT_8888,
                          PSP_DISPLAY_SETBUF_NEXTVSYNC);
    sceDisplayWaitVblankStart();
}

static unsigned char clamp_byte(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (unsigned char)value;
}

static void present_yuv420(const unsigned char *y, const unsigned char *u,
                           const unsigned char *v, int y_stride, int uv_stride,
                           int width, int height) {
    int row, column;
    if (width != VIDEO_WIDTH || height != VIDEO_HEIGHT) return;
    for (row = 0; row < VIDEO_HEIGHT; row++) {
        unsigned char *destination = decoded_frame + row * VIDEO_WIDTH * 4;
        for (column = 0; column < VIDEO_WIDTH; column++) {
            int yy = (int)y[row * y_stride + column] - 16;
            int uu = (int)u[(row >> 1) * uv_stride + (column >> 1)] - 128;
            int vv = (int)v[(row >> 1) * uv_stride + (column >> 1)] - 128;
            int base = yy < 0 ? 0 : 298 * yy;
            destination[column * 4] = clamp_byte((base + 409 * vv + 128) >> 8);
            destination[column * 4 + 1] = clamp_byte((base - 100 * uu - 208 * vv + 128) >> 8);
            destination[column * 4 + 2] = clamp_byte((base + 516 * uu + 128) >> 8);
            destination[column * 4 + 3] = 0xFF;
        }
    }
    sceKernelDcacheWritebackInvalidateAll();
    present_decoded_frame();
}

static int find_aud(const unsigned char *data, int size, int from) {
    int i;
    for (i = from; i + 4 < size; i++) {
        if (data[i] == 0 && data[i + 1] == 0 &&
            ((data[i + 2] == 1 && data[i + 3] == 9) ||
             (data[i + 2] == 0 && data[i + 3] == 1 && data[i + 4] == 9))) return i;
    }
    return -1;
}

static int decode_h264_access_units(int *size, unsigned long long *next_frame_tick) {
    int first, next, result, frames = 0;
    first = find_aud(h264_buffer, *size, 0);
    if (first > 0) {
        memmove(h264_buffer, h264_buffer + first, *size - first);
        *size -= first;
    }
    while ((next = find_aud(h264_buffer, *size, 4)) > 0) {
        if (!hardware_decoder_ready) {
            result = h264_hw_init_from_annexb(h264_buffer, next);
            /* Codec headers precede the first decodable IDR access unit. */
            if (result == -1) {
                memmove(h264_buffer, h264_buffer + next, *size - next);
                *size -= next;
                continue;
            }
            if (result < 0) { video_step = h264_hw_last_step(); return result; }
            hardware_decoder_ready = 1;
            subtitle_parse_prepared_response();
        }
        /* TCP delivers H.264 in bursts.  Drawing every received access unit
         * immediately was the visible catch-up effect.  Never present faster
         * than the server's 20.1 fps, with the audio DAC as master clock. */
        {
            unsigned long long now = sceKernelGetSystemTimeWide();
            if (*next_frame_tick == 0 || now > *next_frame_tick + 100000ULL)
                *next_frame_tick = now;
            if (now < *next_frame_tick)
                sceKernelDelayThread((unsigned int)(*next_frame_tick - now));
        }
        result = h264_hw_decode_annexb(h264_buffer, next, (void *)0x44000000);
        if (result < 0) { video_step = h264_hw_last_step(); return result; }
        if (result > 0) {
            subtitle_present((int)(stream_start_seconds * 20.1f) + hardware_decoder_frames);
            sceDisplaySetFrameBuf((void *)0x04000000, VIDEO_STRIDE, PSP_DISPLAY_PIXEL_FORMAT_8888,
                                  PSP_DISPLAY_SETBUF_NEXTVSYNC);
            sceDisplayWaitVblankStart();
            frames++;
            hardware_decoder_frames++;
            *next_frame_tick += 49751ULL;
        }
        memmove(h264_buffer, h264_buffer + next, *size - next);
        *size -= next;
        /* Keep draining all complete access units already received.  Each
         * iteration is still paced above; returning after one AU made a
         * multi-frame TCP packet grow the H.264 buffer until playback froze. */
    }
    return frames;
}

/* Receive concatenated JPEG images, decode each with the PSP firmware's
 * MJPEG unit, and point the LCD straight at the decoded RGBA frame.  This is
 * intentionally an initial video-only transport: it establishes actual
 * moving pictures before adding the considerably more complex H.264/AAC
 * demux and A/V clock path. */
static int play_mjpeg(const char *media_id) {
    struct sockaddr_in server;
    char request[2048], header[4096], receive_buffer[4096], *body;
    int socket_fd, header_size = 0, received, i, jpeg_size = 0;
    int in_jpeg = 0, previous = 0, frames = 0, result;

    result = load_video_modules();
    if (result < 0) return result;
    video_step = "TCP connection";
    snprintf(request, sizeof(request), "GET /api/transcode/%s?container=mjpeg HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", media_id, server_host);
    socket_fd = sceNetInetSocket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) return socket_fd;
    if (prepare_server(&server) < 0) { sceNetInetClose(socket_fd); return -1206; }
    if (sceNetInetConnect(socket_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        sceNetInetClose(socket_fd); return -1201;
    }
    if ((int)sceNetInetSend(socket_fd, request, strlen(request), 0) < 0) {
        sceNetInetClose(socket_fd); return -1202;
    }
    while (header_size < (int)sizeof(header) - 1) {
        received = (int)sceNetInetRecv(socket_fd, header + header_size, sizeof(header) - 1 - header_size, 0);
        if (received <= 0) { sceNetInetClose(socket_fd); return -1203; }
        header_size += received;
        header[header_size] = '\0';
        body = strstr(header, "\r\n\r\n");
        if (body) break;
    }
    if (!body || !strstr(header, " 200 ")) { video_step = "HTTP response"; sceNetInetClose(socket_fd); return -1204; }
    video_step = "JPEG-Decoder";
    result = sceJpegInitMJpeg();
    if (result < 0) { sceNetInetClose(socket_fd); return result; }
    result = sceJpegCreateMJpeg(VIDEO_WIDTH, VIDEO_HEIGHT);
    if (result < 0) { sceJpegFinishMJpeg(); sceNetInetClose(socket_fd); return result; }

    /* The bytes following the HTTP header are the first JPEG bytes. */
    received = header_size - (int)(body + 4 - header);
    if (received > 0) {
        if (received > (int)sizeof(receive_buffer)) received = sizeof(receive_buffer);
        memcpy(receive_buffer, body + 4, received);
    }
    while (1) {
        if (received <= 0) {
            received = (int)sceNetInetRecv(socket_fd, receive_buffer, sizeof(receive_buffer), 0);
            if (received <= 0) break;
        }
        for (i = 0; i < received; i++) {
            unsigned char byte = (unsigned char)receive_buffer[i];
            if (!in_jpeg) {
                if (previous == 0xFF && byte == 0xD8) {
                    jpeg_buffer[0] = 0xFF; jpeg_buffer[1] = 0xD8;
                    jpeg_size = 2; in_jpeg = 1;
                }
            } else if (jpeg_size < JPEG_BUFFER_BYTES) {
                jpeg_buffer[jpeg_size++] = byte;
                if (previous == 0xFF && byte == 0xD9) {
                    video_step = "JPEG-Dekodierung";
                    result = sceJpegDecodeMJpeg(jpeg_buffer, jpeg_size, decoded_frame, 0);
                    if (result >= 0) {
                        sceKernelDcacheWritebackInvalidateAll();
                        present_decoded_frame();
                        frames++;
                    }
                    in_jpeg = 0; jpeg_size = 0;
                }
            } else {
                in_jpeg = 0; jpeg_size = 0;
            }
            previous = byte;
        }
        received = 0;
    }
    sceJpegDeleteMJpeg();
    sceJpegFinishMJpeg();
    sceNetInetClose(socket_fd);
    if (!frames) video_step = "no JPEG frames";
    return frames ? frames : -1205;
}

static int audio_output_thread(SceSize args, void *argp) {
    int channel, block;
    const int block_bytes = AUDIO_BLOCK_SAMPLES * 2 * (int)sizeof(short);
    (void)args; (void)argp;
    /* MP3 is decoded natively at 44.1 kHz.  Use the regular DAC channel at
     * that exact rate instead of sending it through the SRC mixer.  On this
     * PSP the SRC path leaves a small, periodic seam between DMA blocks that
     * is especially audible in speech as a "tok-tok" artefact. */
    sceAudioSRCChRelease();
    channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, AUDIO_BLOCK_SAMPLES, PSP_AUDIO_FORMAT_STEREO);
    if (channel < 0) { audio_state = -16; audio_running = 0; return 0; }
    while (audio_running || audio_queue_count > 0) {
        while ((audio_running && !audio_start) || audio_queue_count <= 0) {
            if (!audio_running && audio_queue_count <= 0) break;
            sceKernelDelayThread(1000);
        }
        if (!audio_start || audio_queue_count <= 0) continue;
        if (!audio_clock_started) audio_clock_started = 1;
        block = audio_queue_read;
        audio_queue_read = (audio_queue_read + 1) % AUDIO_QUEUE_BLOCKS;
        /* The producer cannot reuse this slot until OutputBlocking returns:
         * count remains unchanged while the DSP owns the DMA buffer. */
        sceKernelDcacheWritebackRange(audio_samples + block * AUDIO_BLOCK_SAMPLES * 2, block_bytes);
        if (sceAudioOutputBlocking(channel, PSP_AUDIO_VOLUME_MAX,
                                   audio_samples + block * AUDIO_BLOCK_SAMPLES * 2) < 0) {
            audio_state = -20; audio_running = 0; break;
        }
        audio_queue_count--;
        audio_state = 16;
    }
    sceAudioChRelease(channel);
    return 0;
}

static int mp3_frame_size(const unsigned char *data, int size) {
    static const int bitrates[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    int bitrate, padding;
    if (size < 4 || data[0] != 0xff || (data[1] & 0xfe) != 0xfa ||
        (data[2] & 0xf0) == 0 || (data[2] & 0xf0) == 0xf0 || (data[2] & 0x0c) != 0) return -1;
    bitrate = bitrates[data[2] >> 4] * 1000;
    padding = (data[2] >> 1) & 1;
    return (144 * bitrate) / 44100 + padding;
}

static int audio_thread(SceSize args, void *argp) {
    struct sockaddr_in server;
    char request[2048], header[4096], *body;
    int socket_fd = -1, header_size = 0, received, output_thread_id = -1;
    int have = 0, frame_size, result, initial_size, frames_in_block = 0;
    const int block_bytes = AUDIO_BLOCK_SAMPLES * 2 * (int)sizeof(short);
    const int decoded_bytes = MP3_DECODE_SAMPLES * 2 * (int)sizeof(short);
    (void)args; (void)argp;
    audio_state = 10;
    memset(mp3_codec, 0, sizeof(mp3_codec));
    if (sceAudiocodecCheckNeedMem(mp3_codec, PSP_CODEC_MP3) < 0) { audio_state = -21; return 0; }
    /* The firmware codec's ME-side DMA touches whole cache lines; reserve a
     * rounded work area, not merely the nominal byte count it reports. */
    mp3_codec_work = memalign(64, (mp3_codec[4] + 63) & ~63UL);
    if (!mp3_codec_work) { audio_state = -22; return 0; }
    mp3_codec[3] = (unsigned long)mp3_codec_work;
    if (sceAudiocodecInit(mp3_codec, PSP_CODEC_MP3) < 0) { audio_state = -23; goto cleanup; }
    snprintf(request, sizeof(request), "GET /api/transcode/%s?container=mp3&profile=%s&audio=%d&audio_quality=%s&start=%d HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", audio_media_id, PSP_STREAMER_PROFILE, selected_audio_track, audio_quality_name(), stream_start_seconds, server_host);
    socket_fd = sceNetInetSocket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) { audio_state = -11; goto cleanup; }
    audio_socket_fd = socket_fd;
    if (prepare_server(&server) < 0) { audio_state = -17; goto cleanup; }
    if (sceNetInetConnect(socket_fd, (struct sockaddr *)&server, sizeof(server)) < 0) { audio_state = -12; goto cleanup; }
    if ((int)sceNetInetSend(socket_fd, request, strlen(request), 0) < 0) { audio_state = -13; goto cleanup; }
    while (header_size < (int)sizeof(header) - 1) {
        received = (int)sceNetInetRecv(socket_fd, header + header_size, sizeof(header) - 1 - header_size, 0);
        if (received <= 0) { audio_state = -14; goto cleanup; }
        header_size += received; header[header_size] = '\0'; body = strstr(header, "\r\n\r\n");
        if (body) break;
    }
    if (!body || !strstr(header, " 200 ")) { audio_state = -15; goto cleanup; }
    initial_size = header_size - (int)(body + 4 - header);
    if (initial_size > MP3_INPUT_BUFFER_BYTES) initial_size = MP3_INPUT_BUFFER_BYTES;
    if (initial_size > 0) memcpy(mp3_input_buffer, body + 4, initial_size);
    have = initial_size;
    audio_queue_read = audio_queue_write = audio_queue_count = 0;
    output_thread_id = sceKernelCreateThread("PSPStreamerDAC", audio_output_thread, 0x10, 0x2000, 0, NULL);
    if (output_thread_id < 0) { audio_state = -16; audio_running = 0; goto cleanup; }
    audio_output_thread_id = output_thread_id;
    sceKernelStartThread(output_thread_id, 0, NULL);
    while (audio_running) {
        if (audio_queue_count >= AUDIO_QUEUE_BLOCKS) { sceKernelDelayThread(1000); continue; }
        while (have < 4 && audio_running) {
            received = stream_recv(socket_fd, mp3_input_buffer + have, MP3_INPUT_BUFFER_BYTES - have, 250);
            if (received == -2) continue;
            if (received <= 0) { audio_running = 0; break; }
            have += received;
        }
        if (!audio_running) break;
        frame_size = mp3_frame_size(mp3_input_buffer, have);
        if (frame_size < 0 || frame_size > MP3_MAX_FRAME_BYTES) { memmove(mp3_input_buffer, mp3_input_buffer + 1, --have); continue; }
        while (have < frame_size && audio_running) {
            received = stream_recv(socket_fd, mp3_input_buffer + have, MP3_INPUT_BUFFER_BYTES - have, 250);
            if (received == -2) continue;
            if (received <= 0) { audio_running = 0; break; }
            have += received;
        }
        if (!audio_running) break;
        mp3_codec[6] = (unsigned long)mp3_input_buffer;
        mp3_codec[7] = mp3_codec[10] = frame_size;
        mp3_codec[8] = (unsigned long)(audio_samples + audio_queue_write * AUDIO_BLOCK_SAMPLES * 2 +
                                        frames_in_block * MP3_DECODE_SAMPLES * 2);
        mp3_codec[9] = decoded_bytes;
        sceKernelDcacheWritebackRange(mp3_input_buffer, frame_size);
        result = sceAudiocodecDecode(mp3_codec, PSP_CODEC_MP3);
        if (result < 0) { audio_state = -24; audio_running = 0; break; }
        memmove(mp3_input_buffer, mp3_input_buffer + frame_size, have - frame_size);
        have -= frame_size;
        frames_in_block++;
        if (frames_in_block == MP3_FRAMES_PER_AUDIO_BLOCK) {
            sceKernelDcacheInvalidateRange(audio_samples + audio_queue_write * AUDIO_BLOCK_SAMPLES * 2, block_bytes);
            sceKernelDcacheWritebackRange(audio_samples + audio_queue_write * AUDIO_BLOCK_SAMPLES * 2, block_bytes);
            audio_queue_write = (audio_queue_write + 1) % AUDIO_QUEUE_BLOCKS;
            audio_queue_count++;
            frames_in_block = 0;
            if (audio_queue_count >= AUDIO_PREFILL_BLOCKS) audio_state = 15;
        }
    }
cleanup:
    if (audio_socket_fd == socket_fd) { audio_socket_fd = -1; if (socket_fd >= 0) sceNetInetClose(socket_fd); }
    if (mp3_codec_work) { free(mp3_codec_work); mp3_codec_work = NULL; }
    return 0;
}

static int play_h264(const char *media_id) {
    struct sockaddr_in server;
    char request[2048], header[4096], receive_buffer[4096], *body;
    int socket_fd, header_size = 0, received, h264_size = 0, frames = 0, result, buffered = 0, wait;
    int audio_thread_id = -1;
    unsigned long long next_frame_tick = 0;
    unsigned long long last_packet_tick;
    unsigned int previous_buttons = 0;
    int paused = 0;
    playback_reached_end = 0;
    /* Text subtitle extraction is independent of the H.264 transcode and
     * normally completes in a fraction of a second.  If it is unavailable,
     * retain the established server burn-in path rather than losing subtitles. */
    prepare_client_subtitles(media_id);
    result = load_video_modules();
    if (result < 0) return result;
    if (hardware_runtime_result != 0) { video_step = "Media-Engine Bridge"; return hardware_runtime_result; }
    video_step = "Hardware-AVC";
    hardware_decoder_ready = 0;
    hardware_decoder_frames = 0;
    strncpy(audio_media_id, media_id, sizeof(audio_media_id) - 1);
    audio_media_id[sizeof(audio_media_id) - 1] = '\0';
    audio_start = 0;
    audio_clock_started = 0;
    /* Start both server transcodes together.  The audio thread fills one DMA
     * block but deliberately stays muted until the first video frame is on
     * screen, which gives them a common practical start point. */
    audio_running = 1;
    audio_state = 0;
    audio_thread_id = sceKernelCreateThread("PSPStreamerAudio", audio_thread,
                                            0x18, 0x4000, 0, NULL);
    if (audio_thread_id >= 0) sceKernelStartThread(audio_thread_id, 0, NULL);
    else { audio_running = 0; audio_state = audio_thread_id; }
    video_step = "TCP connection";
    snprintf(request, sizeof(request), "GET /api/transcode/%s?container=h264&profile=%s&audio=%d&subtitle=%d&start=%d HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", media_id, PSP_STREAMER_PROFILE, selected_audio_track, subtitle_client_side ? -1 : selected_subtitle_track, stream_start_seconds, server_host);
    socket_fd = sceNetInetSocket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) { h264_hw_shutdown(); return socket_fd; }
    if (prepare_server(&server) < 0) { sceNetInetClose(socket_fd); h264_hw_shutdown(); return -1307; }
    if (sceNetInetConnect(socket_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        sceNetInetClose(socket_fd); h264_hw_shutdown(); return -1301;
    }
    if ((int)sceNetInetSend(socket_fd, request, strlen(request), 0) < 0) {
        sceNetInetClose(socket_fd); h264_hw_shutdown(); return -1302;
    }
    while (header_size < (int)sizeof(header) - 1) {
        received = (int)sceNetInetRecv(socket_fd, header + header_size, sizeof(header) - 1 - header_size, 0);
        if (received <= 0) { sceNetInetClose(socket_fd); h264_hw_shutdown(); return -1303; }
        header_size += received; header[header_size] = '\0';
        body = strstr(header, "\r\n\r\n");
        if (body) break;
    }
    if (!body || !strstr(header, " 200 ")) {
        video_step = "HTTP response"; sceNetInetClose(socket_fd); h264_hw_shutdown(); return -1304;
    }
    received = header_size - (int)(body + 4 - header);
    if (received > 0) memcpy(receive_buffer, body + 4, received);
    last_packet_tick = sceKernelGetSystemTimeWide();
    while (1) {
        SceCtrlData pad;
        keep_awake();
        sceCtrlPeekBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_START) { result = frames; break; }
        if ((pad.Buttons & PSP_CTRL_SELECT) && !(previous_buttons & PSP_CTRL_SELECT)) {
            paused = !paused;
            audio_start = paused ? 0 : 1;
        }
        if (!paused && (pad.Buttons & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) &&
            !(previous_buttons & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER))) {
            int delta = (pad.Buttons & PSP_CTRL_RTRIGGER) ? 10 : -10;
            stream_start_seconds += frames / 20 + delta;
            if (stream_start_seconds < 0) stream_start_seconds = 0;
            strncpy(resume_media_id, media_id, sizeof(resume_media_id) - 1);
            resume_media_id[sizeof(resume_media_id) - 1] = '\0';
            resume_pending = 1;
            seek_requested = 1;
            result = frames;
            break;
        }
        previous_buttons = pad.Buttons;
        if (paused) { sceKernelDelayThread(75000); continue; }
        if (received <= 0) {
            /* FFmpeg sends HTTP headers before libass prepares the first
             * subtitle frame.  Until the initial video runway exists, wait
             * normally; only active playback gets interruption polling. */
            if (!buffered) {
                received = (int)sceNetInetRecv(socket_fd, receive_buffer, sizeof(receive_buffer), 0);
            } else {
                received = stream_recv(socket_fd, receive_buffer, sizeof(receive_buffer), 250);
                if (received == -2) {
                    if (sceKernelGetSystemTimeWide() - last_packet_tick < 5000000ULL) continue;
                    received = 0;
                }
            }
            if (received > 0) last_packet_tick = sceKernelGetSystemTimeWide();
            if (received <= 0) {
                /* TCP EOF can also be a broken WLAN connection.  Advance
                 * only after nearly the full known duration was rendered. */
                if (received == 0 && current_duration_seconds > 0.0f &&
                    (float)frames >= current_duration_seconds * 20.0f * 0.90f)
                    playback_reached_end = 1;
                if (!playback_reached_end) {
                    stream_start_seconds += frames / 20;
                    if (stream_start_seconds > 2) stream_start_seconds -= 2;
                    strncpy(resume_media_id, media_id, sizeof(resume_media_id) - 1);
                    resume_media_id[sizeof(resume_media_id) - 1] = '\0';
                    resume_pending = 1;
                }
                break;
            }
        }
        if (h264_size + received > H264_BUFFER_BYTES) {
            video_step = "H264-Puffer"; result = -1305; break;
        }
        memcpy(h264_buffer + h264_size, receive_buffer, received);
        h264_size += received;
        /* A small warm-up absorbs the connection start without accumulating
         * so many frames that playback has to catch up afterwards. */
        if (!buffered && h264_size < 16 * 1024) { received = 0; continue; }
        if (!buffered && audio_running) {
            /* Do not release either clock until audio has its initial runway. */
            for (wait = 0; wait < 100 && audio_state < 15 && audio_running; wait++)
                sceKernelDelayThread(10000);
        }
        if (!audio_start) {
            /* Release the DAC first; it is the master clock for the video
             * presentation deadlines that follow. */
            audio_start = 1;
            for (wait = 0; wait < 20 && !audio_clock_started; wait++)
                sceKernelDelayThread(1000);
            next_frame_tick = sceKernelGetSystemTimeWide();
        }
        buffered = 1;
        result = decode_h264_access_units(&h264_size, &next_frame_tick);
        if (result < 0) break;
        if (result > 0 && !audio_start) audio_start = 1;
        frames += result;
        received = 0;
    }
    audio_running = 0;
    audio_start = 1;
    audio_queue_count = 0;
    /* Wake a blocking receive before returning to the browser.  Otherwise
     * it retains the firmware MP3 handle and the next film is silent. */
    if (audio_socket_fd >= 0) {
        int closing_socket = audio_socket_fd;
        audio_socket_fd = -1;
        sceNetInetClose(closing_socket);
    }
    if (audio_thread_id >= 0) sceKernelWaitThreadEnd(audio_thread_id, NULL);
    if (audio_output_thread_id >= 0) {
        sceKernelWaitThreadEnd(audio_output_thread_id, NULL);
        audio_output_thread_id = -1;
    }
    h264_hw_shutdown();
    subtitle_release();
    sceNetInetClose(socket_fd);
    if (result < 0) return result;
    if (!frames) video_step = "no H.264 frames";
    return frames ? frames : -1306;
}

/* This is intentionally a narrow parser for our own compact JSON response. */
static int json_value(const char *from, const char *key, char *destination, size_t length) {
    char needle[24];
    const char *start, *end;
    snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    start = strstr(from, needle);
    if (!start) return 0;
    start += strlen(needle);
    end = strchr(start, '\"');
    if (!end) return 0;
    size_t value_length = (size_t)(end - start);
    if (value_length >= length) value_length = length - 1;
    memcpy(destination, start, value_length);
    destination[value_length] = '\0';
    return 1;
}

static void parse_stream_tracks(const char *array_key, StreamTrack *tracks, int *count) {
    char *cursor = strstr(response, array_key);
    *count = 0;
    if (!cursor || !(cursor = strchr(cursor, '['))) return;
    cursor++;
    while (*count < 8) {
        char number[16];
        char *object = strchr(cursor, '{');
        if (!object || strchr(cursor, ']') < object) break;
        if (!json_value(object, "n", number, sizeof(number)) ||
            !json_value(object, "l", tracks[*count].language, sizeof(tracks[*count].language))) break;
        tracks[*count].number = atoi(number);
        (*count)++;
        cursor = strchr(object, '}');
        if (!cursor) break;
        cursor++;
    }
}

static void load_media_metadata(const char *media_id) {
    char path[ID_SIZE + 32], duration[24];
    int result;
    snprintf(path, sizeof(path), "/api/metadata/%s", media_id);
    current_duration_seconds = 0.0f;
    result = http_get(path, response, sizeof(response));
    audio_track_count = subtitle_track_count = 0;
    if (result < 0) return;
    parse_stream_tracks("\"a\":[", audio_tracks, &audio_track_count);
    parse_stream_tracks("\"s\":[", subtitle_tracks, &subtitle_track_count);
    current_duration_seconds = json_value(response, "d", duration, sizeof(duration)) ? (float)atof(duration) : 0.0f;
    if (audio_track_count && selected_audio_track >= audio_track_count) selected_audio_track = 0;
    if (!audio_track_count) selected_audio_track = 0;
    if (selected_subtitle_track >= subtitle_track_count) selected_subtitle_track = -1;
    /* The usual preferred subtitle is fetched while the visible options
     * screen is being prepared.  The server caches its finite extraction, so
     * pressing X afterwards does not hold up the H.264 startup. */
    if (selected_subtitle_track >= 0) prepare_client_subtitles(media_id);
}

static void show_metadata_loading(void) {
    pspDebugScreenClear();
    pspDebugScreenSetTextColor(0x00D8E8FF);
    pspDebugScreenPrintf("Playback options\n\n");
    pspDebugScreenSetTextColor(0x00FFFFFF);
    pspDebugScreenPrintf("Loading audio tracks and subtitles ...\n");
    pspDebugScreenPrintf("Please wait.\n");
}

static void parse_library(void) {
    char *cursor;
    item_count = 0;
    cursor = strstr(response, "\"folders\":[");
    if (cursor) cursor = strchr(cursor, '[') + 1;
    while (cursor && item_count < MAX_ITEMS) {
        char *object = strchr(cursor, '{');
        if (!object || !json_value(object, "name", items[item_count].title, TITLE_SIZE)) break;
        if (!json_value(object, "path", items[item_count].value, ID_SIZE)) break;
        items[item_count].is_folder = 1;
        item_count++;
        cursor = strchr(object, '}');
        if (!cursor) break;
        cursor++;
    }
    cursor = strstr(response, "\"videos\":[");
    if (cursor) cursor = strchr(cursor, '[') + 1;
    while (cursor && item_count < MAX_ITEMS) {
        char *object = strchr(cursor, '{');
        if (!object || !json_value(object, "name", items[item_count].title, TITLE_SIZE)) break;
        if (!json_value(object, "id", items[item_count].value, ID_SIZE)) break;
        items[item_count].is_folder = 0;
        item_count++;
        cursor = strchr(object, '}');
        if (!cursor) break;
        cursor++;
    }
}

static void url_encode(const char *source, char *destination, size_t length) {
    static const char hex[] = "0123456789ABCDEF";
    size_t written = 0;
    unsigned char c;
    while ((c = (unsigned char)*source++) != '\0' && written + 4 < length) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' || c == '.') {
            destination[written++] = (char)c;
        } else {
            destination[written++] = '%';
            destination[written++] = hex[c >> 4];
            destination[written++] = hex[c & 15];
        }
    }
    destination[written] = '\0';
}

static void parent_path(void) {
    char *last = strrchr(current_path, '/');
    if (last) *last = '\0'; else current_path[0] = '\0';
}

static int next_video_index(int selected) {
    int index;
    for (index = selected + 1; index < item_count; index++)
        if (!items[index].is_folder) return index;
    return -1;
}

static void refresh_library(void) {
    int result;
    char encoded_path[ID_SIZE * 3 + 1];
    char api_path[ID_SIZE * 3 + 32];
    if (!network_ready || !http_ready) {
        strcpy(status, "Wi-Fi/HTTP not ready yet");
        return;
    }
    strcpy(status, "Loading library ...");
    url_encode(current_path, encoded_path, sizeof(encoded_path));
    snprintf(api_path, sizeof(api_path), "/api/library?path=%s", encoded_path);
    result = http_get(api_path, response, sizeof(response));
    if (result < 0) {
        snprintf(status, sizeof(status), "Server error: %08X", result);
        /* A just-restored hotspot often has IP before DNS.  Keep the useful
         * browser state visible so Square can simply be tried again. */
        return;
    }
    parse_library();
    snprintf(status, sizeof(status), "%d entries  [X: Open]", item_count);
}

static void show(int selected) {
    int i, first = (selected / LIST_ROWS) * LIST_ROWS;
    int last = first + LIST_ROWS;
    if (last > item_count) last = item_count;
    pspDebugScreenClear();
    pspDebugScreenSetTextColor(0x00D8E8FF);
    pspDebugScreenPrintf("PSP Streamer  |  %.32s:%d  Profile %d\n\n", server_host, server_port, active_network_profile);
    pspDebugScreenSetTextColor(0x00FFFFFF);
    pspDebugScreenPrintf("%s\n\n", status);
    if (hardware_runtime_result == 0)
        pspDebugScreenPrintf("Media Engine: Hardware AVC ready\n");
    else if (hardware_runtime_result != -9999)
        pspDebugScreenPrintf("Media Engine: %s: %08X\nBridge: %.57s\n", hardware_runtime_step, hardware_runtime_result, hardware_runtime_path);
    pspDebugScreenPrintf("\n");
    pspDebugScreenPrintf("Path: %.52s\n\n", current_path[0] ? current_path : "/");
    if (!item_count) {
        pspDebugScreenPrintf("No entries. Square: reload\n");
    } else {
        for (i = first; i < last; i++) {
            pspDebugScreenSetTextColor(i == selected ? 0x00D8E8FF : 0x00FFFFFF);
            pspDebugScreenPrintf("%c %c %.50s\n", i == selected ? '>' : ' ', items[i].is_folder ? '+' : '*', items[i].title);
        }
    }
    pspDebugScreenSetTextColor(0x00FFFFFF);
    pspDebugScreenPrintf("\nControls: UP/DOWN select, X opens, LEFT goes back,\nSquare reloads, START exits.\n");
}

/* A compact pre-playback dialog.  Track numbers follow ffprobe/ffmpeg's
 * stream order; a later metadata pass can attach language labels without
 * changing the streaming protocol. */
static int playback_options(void) {
    SceCtrlData pad;
    unsigned int old = 0;
    int row = 0;
    /* The dialog is opened with X.  Consume that press first, otherwise the
     * first controller poll treats the still-held button as "Start". */
    do {
        sceCtrlReadBufferPositive(&pad, 1);
        sceKernelDelayThread(10000);
    } while (pad.Buttons & PSP_CTRL_CROSS);
    while (1) {
        pspDebugScreenClear();
        pspDebugScreenSetTextColor(0x00D8E8FF);
        pspDebugScreenPrintf("Playback options\n\n");
        pspDebugScreenSetTextColor(0x00FFFFFF);
        pspDebugScreenPrintf("%c Audio track:   %s\n", row == 0 ? '>' : ' ',
                             audio_track_count ? audio_tracks[selected_audio_track].language : "not detected");
        pspDebugScreenPrintf("%c Subtitles:     %s\n", row == 1 ? '>' : ' ',
                             selected_subtitle_track < 0 ? "Off" : subtitle_tracks[selected_subtitle_track].language);
        pspDebugScreenPrintf("%c Audio quality: %s\n\n", row == 2 ? '>' : ' ', audio_quality_name());
        pspDebugScreenPrintf("UP/DOWN: row  LEFT/RIGHT: change\nX: Start   O: Back\n");
        sceCtrlReadBufferPositive(&pad, 1);
        if ((pad.Buttons & PSP_CTRL_CIRCLE) && !(old & PSP_CTRL_CIRCLE)) return 0;
        if ((pad.Buttons & PSP_CTRL_CROSS) && !(old & PSP_CTRL_CROSS)) return 1;
        if ((pad.Buttons & PSP_CTRL_UP) && !(old & PSP_CTRL_UP)) row = (row + 2) % 3;
        if ((pad.Buttons & PSP_CTRL_DOWN) && !(old & PSP_CTRL_DOWN)) row = (row + 1) % 3;
        if ((pad.Buttons & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) && !(old & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT))) {
            int delta = (pad.Buttons & PSP_CTRL_RIGHT) ? 1 : -1;
            if (row == 0 && audio_track_count)
                selected_audio_track = (selected_audio_track + delta + audio_track_count) % audio_track_count;
            else if (row == 1) {
                if (subtitle_track_count) {
                    selected_subtitle_track += delta;
                    if (selected_subtitle_track < -1) selected_subtitle_track = subtitle_track_count - 1;
                    if (selected_subtitle_track >= subtitle_track_count) selected_subtitle_track = -1;
                }
            } else selected_audio_quality = (selected_audio_quality + delta + 3) % 3;
            save_playback_settings();
        }
        old = pad.Buttons;
        sceKernelDelayThread(75000);
    }
}

int main(void) {
    SceCtrlData pad;
    unsigned int old_buttons = 0;
    unsigned long long next_repeat_tick = 0;
    int selected = 0;
    int dirty = 1;
    int result;
    setup_callbacks();
    load_playback_settings();
    pspDebugScreenInit();
    pspDebugScreenSetXY(0, 0);
    /* ARK-5's true overclock is managed by its own runlevel setting.  The
     * legacy systemctrl speed API tops out at the Sony 333-MHz range, so do
     * not call it here and accidentally undo a Homebrew overclock. */
    performance_result = 0;
    result = wait_for_network();
    if (result < 0) {
        snprintf(status, sizeof(status), "%s failed: %08X", failure_step, result);
    } else {
        network_ready = 1;
        http_ready = 1;
        /* Preflight only: no hardware AVC calls are made in this build. */
        hardware_runtime_result = load_hardware_avc_runtime();
        refresh_library();
    }
    while (1) {
        keep_awake();
        sceCtrlReadBufferPositive(&pad, 1);
        if ((pad.Buttons & PSP_CTRL_START) && !(old_buttons & PSP_CTRL_START)) break;
        if ((pad.Buttons & PSP_CTRL_SQUARE) && !(old_buttons & PSP_CTRL_SQUARE)) { refresh_library(); dirty = 1; }
        if (item_count && (pad.Buttons & (PSP_CTRL_DOWN | PSP_CTRL_UP))) {
            unsigned long long now = sceKernelGetSystemTimeWide();
            unsigned int direction = pad.Buttons & (PSP_CTRL_DOWN | PSP_CTRL_UP);
            if (!(old_buttons & direction) || now >= next_repeat_tick) {
                if (direction & PSP_CTRL_DOWN) selected = (selected + 1) % item_count;
                else selected = (selected + item_count - 1) % item_count;
                next_repeat_tick = now + 150000ULL;
                dirty = 1;
            }
        } else next_repeat_tick = 0;
        if (item_count && (pad.Buttons & PSP_CTRL_RTRIGGER) && !(old_buttons & PSP_CTRL_RTRIGGER)) {
            selected = (selected + LIST_ROWS) % item_count; dirty = 1;
        }
        if (item_count && (pad.Buttons & PSP_CTRL_LTRIGGER) && !(old_buttons & PSP_CTRL_LTRIGGER)) {
            selected = (selected + item_count - (LIST_ROWS % item_count)) % item_count; dirty = 1;
        }
        if ((pad.Buttons & PSP_CTRL_LEFT) && !(old_buttons & PSP_CTRL_LEFT) && current_path[0]) { parent_path(); selected = 0; refresh_library(); dirty = 1; }
        if (item_count && (pad.Buttons & PSP_CTRL_CROSS) && !(old_buttons & PSP_CTRL_CROSS) && items[selected].is_folder) {
            strncpy(current_path, items[selected].value, sizeof(current_path) - 1);
            current_path[sizeof(current_path) - 1] = '\0';
            selected = 0;
            refresh_library();
            dirty = 1;
        } else if (item_count && (pad.Buttons & PSP_CTRL_CROSS) && !(old_buttons & PSP_CTRL_CROSS) && !items[selected].is_folder) {
            if (resume_pending && !strcmp(resume_media_id, items[selected].value)) {
                snprintf(status, sizeof(status), "Resuming at %d s ...", stream_start_seconds);
                show(selected);
                if (wait_for_network_restore() < 0) {
                    strcpy(status, "Wi-Fi not ready - press X again");
                    dirty = 1;
                    old_buttons = pad.Buttons;
                    continue;
                }
            } else {
                /* ffprobe may briefly wake a sleeping SMB disk.  Give that
                 * synchronous query a visible state rather than looking frozen. */
                resume_pending = 0;
                stream_start_seconds = 0;
                show_metadata_loading();
                load_media_metadata(items[selected].value);
                if (!playback_options()) { dirty = 1; old_buttons = pad.Buttons; continue; }
            }
            do {
                int next;
                snprintf(status, sizeof(status), "Starting H.264 video (20.1 FPS) ...");
                show(selected);
                result = play_h264(items[selected].value);
                pspDebugScreenInit();
                if (result < 0) {
                    snprintf(status, sizeof(status), "%s: %08X", video_step, result);
                    break;
                }
                if (resume_pending && !playback_reached_end) {
                    if (seek_requested) {
                        seek_requested = 0;
                        /* The PSP firmware releases a finished audio thread
                         * lazily.  Give it one scheduler slice before a new
                         * decoder/DAC pair is allocated for the seek. */
                        sceKernelDelayThread(250000);
                        continue;
                    }
                    snprintf(status, sizeof(status), "Wi-Fi interrupted. Reconnect Wi-Fi, press X to resume.");
                    break;
                }
                resume_pending = 0;
                next = playback_reached_end ? next_video_index(selected) : -1;
                if (next < 0) {
                    snprintf(status, sizeof(status), "Video ended: %d frames | Audio %d", result, audio_state);
                    break;
                }
                selected = next;
                resume_pending = 0;
                stream_start_seconds = 0;
                snprintf(status, sizeof(status), "Next episode: %.58s", items[selected].title);
                show(selected);
                sceKernelDelayThread(500000);
                load_media_metadata(items[selected].value);
            } while (1);
            dirty = 1;
        }
        old_buttons = pad.Buttons;
        if (dirty) { show(selected); dirty = 0; }
        sceKernelDelayThread(75000);
    }
    sceNetApctlTerm();
    sceNetInetTerm();
    sceNetTerm();
    sceUtilityUnloadNetModule(PSP_NET_MODULE_INET);
    sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
    sceKernelExitGame();
    return 0;
}
