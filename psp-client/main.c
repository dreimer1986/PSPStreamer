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
#include <stdarg.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "config.h"
#include "h264_hw.h"
#include "language.h"

PSP_MODULE_INFO("PSPStreamer", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define RESPONSE_SIZE (512 * 1024)
/* The PSP-3000 has 64 MiB RAM.  Keep a generously sized, paged directory
 * index so large anime/series roots are browsable instead of silently cut at
 * 24 entries. */
#define MAX_ITEMS 1024
#define LIST_ROWS 20
#define GUI_LIST_ROWS 11
#define H264_BUFFER_BYTES (768 * 1024)
#define VIDEO_WIDTH 480
#define VIDEO_HEIGHT 272
#define VIDEO_STRIDE 512
#define TITLE_SIZE 128
#define MENU_SKIN_BYTES (VIDEO_WIDTH * VIDEO_HEIGHT * 4)
/* IDs encode the complete relative path and can be long for episode files. */
#define ID_SIZE 512
#define SCE_ERROR_LIBRARY_ALREADY_EXISTS ((int)0x8002013B)
/* sceUtilityLoadModule reports this when AV_MP3 is already resident. */
#define SCE_ERROR_UTILITY_MODULE_LOADED ((int)0x80111102)

typedef struct {
    char title[TITLE_SIZE];
    char value[ID_SIZE];
    int is_folder;
    int is_audio;
} LibraryItem;

typedef struct {
    int number;
    char language[16];
    char title[48];
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
typedef struct { int start, end, x, y, width, height, canvas_width, canvas_height; } BitmapCue;

static char response[RESPONSE_SIZE];
/* Menu-only artwork is kept outside EBOOT so the actual video player stays
 * small.  It occupies 510 KiB only while the application is running. */
extern unsigned char receiver_skin[];
extern unsigned char receiver_skin_end[];
static const unsigned char *menu_skin;
/* 512 pixels is the required power-of-two display stride. */
static unsigned char h264_buffer[H264_BUFFER_BYTES] __attribute__((aligned(64)));
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
#define AUDIO_MUSIC_PREFILL_BLOCKS 4
#define AUDIO_QUEUE_BLOCKS 8
#define SPECTRUM_BANDS 12
#define MP3_INPUT_BUFFER_BYTES 4096
#define MP3_MAX_FRAME_BYTES 576
/* Producer/consumer queue: network jitter is absorbed here while the output
 * thread feeds the DSP on time. */
static short audio_samples[AUDIO_BLOCK_SAMPLES * 2 * AUDIO_QUEUE_BLOCKS] __attribute__((aligned(64)));
/* Direct firmware codec input and its required work area.  Unlike sceMp3,
 * this path has no fake file offsets or opaque streaming ring. */
static unsigned char mp3_input_buffer[MP3_INPUT_BUFFER_BYTES] __attribute__((aligned(64)));
/* Keep the Media Engine's decode target separate from DAC-owned ring slots.
 * PMPlayer Advance uses this exact staging arrangement; it prevents a late
 * ME cache/DMA write from ever touching a buffer being recycled by audio. */
static short mp3_decode_pcm[MP3_DECODE_SAMPLES * 2] __attribute__((aligned(64)));
static unsigned long mp3_codec[65] __attribute__((aligned(64)));
static void *mp3_codec_work;
static volatile int audio_queue_read, audio_queue_write;
static volatile int audio_queue_primed;
static volatile int audio_blocks_published;
/* The PCM ring has two explicit ownership semaphores.  A slot is either
 * owned by the decoder (free) or by the DAC (ready), never inferred solely
 * from a concurrently changed counter.  This is the same bounded-ring model
 * used by PMPlayer Advance. */
static SceUID audio_queue_free_sema = -1;
static SceUID audio_queue_ready_sema = -1;
static volatile int audio_played_blocks;
/* Video keeps its proven two-block lead.  Stand-alone music can afford a
 * deeper runway before the DAC starts, absorbing Wi-Fi/FFmpeg jitter. */
static volatile int audio_prefill_target = AUDIO_PREFILL_BLOCKS;
static volatile int audio_dac_samples = AUDIO_BLOCK_SAMPLES;
static volatile int vu_left, vu_right;
/* Separate displayed needles from the instantaneous PCM peaks. */
static int vu_display_left, vu_display_right;
/* A compact real frequency view.  The DAC worker measures the PCM it is
 * about to play; the GUI merely smooths and draws these values. */
static volatile unsigned char spectrum_levels[SPECTRUM_BANDS];
static unsigned char spectrum_display[SPECTRUM_BANDS];
static char status[128] = "Starting network ...";
static int selected_audio_track;
static int selected_subtitle_track = -1;
static int selected_audio_quality = 2;
static int audio_shuffle;
/* 0..30 maps cleanly to the 30 LED detents in the receiver UI. */
static int playback_volume = 24;
static StreamTrack audio_tracks[8], subtitle_tracks[8];
static int audio_track_count, subtitle_track_count;
static SubtitleCue *subtitle_cues;
static int subtitle_cue_count;
static unsigned char *subtitle_font;
static BitmapCue *bitmap_cues;
static int bitmap_cue_count, bitmap_client_side, bitmap_loaded_cue = -1, bitmap_bytes;
static int subtitle_client_side;
static float current_duration_seconds;
static int playback_reached_end;
static volatile int playback_paused;
static int video_fullscreen = 1;
static int receiver_visible;
static unsigned int receiver_flash_button;
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
            else if (!strncmp(line, "volume=", 7)) playback_volume = atoi(line + 7);
            else if (!strncmp(line, "shuffle=", 8)) audio_shuffle = atoi(line + 8) != 0;
            else if (!strncmp(line, "language=", 9)) language_set_code(line + 9);
        }
    }
    if (selected_audio_track < 0 || selected_audio_track > 7) selected_audio_track = 0;
    if (selected_subtitle_track < -1 || selected_subtitle_track > 7) selected_subtitle_track = -1;
    if (selected_audio_quality < 0 || selected_audio_quality > 2) selected_audio_quality = 2;
    audio_shuffle = audio_shuffle != 0;
    if (playback_volume < 0 || playback_volume > 30) playback_volume = 24;
    if (server_port < 1 || server_port > 65535) server_port = PSP_STREAMER_PORT;
    if (!server_host[0]) strcpy(server_host, PSP_STREAMER_HOST);
}

static void save_playback_settings(void) {
    SceUID file;
    char data[192];
    int length = snprintf(data, sizeof(data), "server=%s\nport=%d\naudio=%d\nsubtitle=%d\nquality=%d\nvolume=%d\nshuffle=%d\nlanguage=%s\n",
                          server_host, server_port, selected_audio_track, selected_subtitle_track, selected_audio_quality, playback_volume, audio_shuffle, language_code());
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

static int http_get_wait(const char *path, char *buffer, int buffer_size, int idle_timeout_ms) {
    struct sockaddr_in server;
    char request[2048], *body;
    int socket_fd, received = 0, read_size, content_length = -1, header_length = -1, idle_ms = 0;
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
        read_size = stream_recv(socket_fd, buffer + received, buffer_size - 1 - received, 250);
        if (read_size == -2) {
            idle_ms += 250;
            if (idle_ms < idle_timeout_ms) continue;
            sceNetInetClose(socket_fd);
            return -1005;
        }
        if (read_size <= 0) break;
        idle_ms = 0;
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

static int http_get(const char *path, char *buffer, int buffer_size) {
    /* Browsing and metadata must never strand the UI after a Wi-Fi dropout. */
    return http_get_wait(path, buffer, buffer_size, 20000);
}

static int http_get_binary(const char *path, unsigned char *buffer, int buffer_size) {
    struct sockaddr_in server;
    char request[2048], header[4096], *body;
    int socket_fd, received = 0, header_size = 0, body_size, content_length = -1, idle_ms = 0;
    socket_fd = sceNetInetSocket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0 || prepare_server(&server) < 0) return -1;
    if (sceNetInetConnect(socket_fd, (struct sockaddr *)&server, sizeof(server)) < 0) { sceNetInetClose(socket_fd); return -1; }
    snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, server_host);
    if ((int)sceNetInetSend(socket_fd, request, strlen(request), 0) < 0) { sceNetInetClose(socket_fd); return -1; }
    while (header_size < (int)sizeof(header) - 1) {
        int got = stream_recv(socket_fd, header + header_size, sizeof(header) - 1 - header_size, 250);
        if (got == -2) { if ((idle_ms += 250) < 20000) continue; sceNetInetClose(socket_fd); return -1; }
        if (got <= 0) { sceNetInetClose(socket_fd); return -1; }
        idle_ms = 0;
        header_size += got; header[header_size] = 0; body = strstr(header, "\r\n\r\n"); if (body) break;
    }
    if (!body || !strstr(header, " 200 ")) { sceNetInetClose(socket_fd); return -1; }
    { char *length_header = strstr(header, "Content-Length:"); if (length_header) content_length = atoi(length_header + 15); }
    body += 4; body_size = header_size - (int)(body - header);
    if (body_size > buffer_size) { sceNetInetClose(socket_fd); return -1; }
    memcpy(buffer, body, body_size); received = body_size;
    while (received < buffer_size && (content_length < 0 || received < content_length)) {
        int wanted = buffer_size - received;
        int got;
        if (content_length >= 0 && wanted > content_length - received) wanted = content_length - received;
        got = stream_recv(socket_fd, buffer + received, wanted, 250);
        if (got == -2) { if ((idle_ms += 250) < 20000) continue; sceNetInetClose(socket_fd); return -1; }
        if (got <= 0) break;
        idle_ms = 0; received += got;
    }
    sceNetInetClose(socket_fd); return received;
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
    if (strstr(response, "\"t\":\"bitmap\"")) {
        char path[ID_SIZE + 64], *cursor;
        snprintf(path, sizeof(path), "/api/bitmap-subtitles/%s?track=%d", media_id, selected_subtitle_track);
        if (http_get_wait(path, response, sizeof(response), 180000) < 0 || !strstr(response, "\"t\":\"pgs\"")) return 0;
        if (bitmap_cues) free(bitmap_cues);
        bitmap_cues = memalign(64, 960 * sizeof(*bitmap_cues));
        if (!bitmap_cues) return 0;
        bitmap_cue_count = 0; bitmap_loaded_cue = -1; bitmap_client_side = 1;
        /* Skip the outer cue array.  Starting the parser at its '[' makes
         * sscanf see "[[..." and reject every PGS cue. */
        cursor = strstr(response, "\"c\":["); if (!cursor) return 0;
        cursor = strchr(cursor, '['); if (!cursor) return 0;
        cursor++;
        while (bitmap_cue_count < 960) {
            BitmapCue *cue = &bitmap_cues[bitmap_cue_count]; int used = 0; char *entry = strchr(cursor, '[');
            if (!entry || sscanf(entry, "[%d,%d,%d,%d,%d,%d,%d,%d]%n", &cue->start, &cue->end, &cue->x, &cue->y, &cue->width, &cue->height, &cue->canvas_width, &cue->canvas_height, &used) != 8) break;
            bitmap_cue_count++; cursor = entry + used;
        }
        return 0;
    }
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
    if (bitmap_cues) free(bitmap_cues);
    bitmap_cues = NULL; bitmap_cue_count = 0; bitmap_client_side = 0; bitmap_loaded_cue = -1;
}

static void bitmap_present(int frame, const char *media_id) {
    int index = -1, i, got, x, y, left, top, right, bottom;
    BitmapCue *cue;
    if (!bitmap_client_side || !bitmap_cues) return;
    for (i = 0; i < bitmap_cue_count; i++) if (frame >= bitmap_cues[i].start && frame < bitmap_cues[i].end) { index = i; break; }
    if (index < 0) return;
    cue = &bitmap_cues[index];
    if (bitmap_loaded_cue != index) {
        char path[ID_SIZE + 96];
        snprintf(path, sizeof(path), "/api/bitmap-sprite/%s?track=%d&cue=%d", media_id, selected_subtitle_track, index);
        got = http_get_binary(path, (unsigned char *)response, RESPONSE_SIZE);
        if (got < 1024 + cue->width * cue->height) return;
        bitmap_bytes = got; bitmap_loaded_cue = index;
    }
    /* PGS stores a full-HD, palette-indexed sprite.  Sample it once per PSP
     * output pixel rather than writing every source pixel (often 20 times
     * over the same destination).  This both preserves video headroom and
     * makes the original coloured, antialiased caption legible. */
    left = cue->x * VIDEO_WIDTH / cue->canvas_width;
    top = cue->y * VIDEO_HEIGHT / cue->canvas_height;
    right = (cue->x + cue->width) * VIDEO_WIDTH / cue->canvas_width;
    bottom = (cue->y + cue->height) * VIDEO_HEIGHT / cue->canvas_height;
    if (right > VIDEO_WIDTH) right = VIDEO_WIDTH;
    if (bottom > VIDEO_HEIGHT) bottom = VIDEO_HEIGHT;
    for (y = top; y < bottom; y++) for (x = left; x < right; x++) {
        int source_x = (x * cue->canvas_width + cue->canvas_width / 2) / VIDEO_WIDTH - cue->x;
        int source_y = (y * cue->canvas_height + cue->canvas_height / 2) / VIDEO_HEIGHT - cue->y;
        unsigned char color, alpha, red, green, blue;
        u32 *destination;
        if (source_x < 0) source_x = 0;
        if (source_y < 0) source_y = 0;
        if (source_x >= cue->width) source_x = cue->width - 1;
        if (source_y >= cue->height) source_y = cue->height - 1;
        color = (unsigned char)response[1024 + source_y * cue->width + source_x];
        alpha = (unsigned char)response[color * 4 + 3];
        if (!alpha) continue;
        red = (unsigned char)response[color * 4];
        green = (unsigned char)response[color * 4 + 1];
        blue = (unsigned char)response[color * 4 + 2];
        destination = &((u32 *)0x44000000)[y * VIDEO_STRIDE + x];
        if (alpha == 255) *destination = red | ((u32)green << 8) | ((u32)blue << 16);
        else {
            u32 old = *destination;
            *destination = ((red * alpha + (old & 0xff) * (255 - alpha)) / 255) |
                           (((green * alpha + ((old >> 8) & 0xff) * (255 - alpha)) / 255) << 8) |
                           (((blue * alpha + ((old >> 16) & 0xff) * (255 - alpha)) / 255) << 16);
        }
    }
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
            const char *next = subtitle_draw_line(text, (video_fullscreen || !receiver_visible ? 210 : 12) + y * 20, vram);
            if (next == text) break;
            text = next;
        }
    }
}

/* A deliberately tiny playback HUD: it costs a few VRAM writes, not a second
 * framebuffer or font renderer.  It makes pause state and progress visible
 * while retaining the proven direct Media-Engine presentation path. */
static void playback_hud(int frames, int paused) {
    u32 *vram = (u32 *)0x44000000;
    int x, y = VIDEO_HEIGHT - 5, filled = 0;
    if (current_duration_seconds > 0.0f)
        filled = (int)(VIDEO_WIDTH * (frames + stream_start_seconds * 20.1f) / (current_duration_seconds * 20.1f));
    if (filled < 0) filled = 0;
    if (filled > VIDEO_WIDTH) filled = VIDEO_WIDTH;
    for (x = 0; x < VIDEO_WIDTH; x++)
        vram[y * VIDEO_STRIDE + x] = x < filled ? 0x00D8E8FF : 0x00202020;
    if (paused) {
        for (y = 8; y < 30; y++) for (x = 444; x < 472; x++)
            if ((x >= 448 && x < 456) || (x >= 462 && x < 470))
                vram[y * VIDEO_STRIDE + x] = 0x00FFFFFF;
            else if (x == 444 || x == 471 || y == 8 || y == 29)
                vram[y * VIDEO_STRIDE + x] = 0x00000000;
    }
}

static void gui_rect(u32 *vram, int left, int top, int width, int height, u32 color) {
    int x, y;
    if (left < 0) { width += left; left = 0; }
    if (top < 0) { height += top; top = 0; }
    if (left + width > VIDEO_WIDTH) width = VIDEO_WIDTH - left;
    if (top + height > VIDEO_HEIGHT) height = VIDEO_HEIGHT - top;
    for (y = top; y < top + height; y++) for (x = left; x < left + width; x++)
        vram[y * VIDEO_STRIDE + x] = color;
}

static void gui_line(u32 *vram, int x0, int y0, int x1, int y1, u32 color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy, twice_error;
    while (1) {
        if (x0 >= 0 && x0 < VIDEO_WIDTH && y0 >= 0 && y0 < VIDEO_HEIGHT)
            vram[y0 * VIDEO_STRIDE + x0] = color;
        if (x0 == x1 && y0 == y1) break;
        /* Both decisions must use the same error snapshot.  Reusing the
         * first updated value can walk past the endpoint indefinitely. */
        twice_error = 2 * error;
        if (twice_error >= dy) { error += dy; x0 += sx; }
        if (twice_error <= dx) { error += dx; y0 += sy; }
    }
}

static void receiver_hud(int frames);

/* The SDK's debug font is ideal for diagnostics, but its 8-pixel-wide bold
 * glyphs make a 480-pixel media browser look like a terminal.  Reuse the
 * already shipped Latin-1 atlas at a deliberately slim 6x8 size for UI text.
 * This is direct VRAM drawing, just like subtitles, and is never used while
 * the hardware AVC path is presenting frames. */
static void gui_draw_small_glyph(u32 *vram, int glyph, int left, int top, u32 color) {
    const unsigned char *bitmap;
    int x, y;
    if (!subtitle_font || glyph < 0 || glyph > 255) return;
    bitmap = subtitle_font + (glyph >> 4) * SUBTITLE_FONT_CELL_HEIGHT * (SUBTITLE_FONT_CELL_WIDTH * 16) +
             (glyph & 15) * SUBTITLE_FONT_CELL_WIDTH;
    for (y = 0; y < 8; y++) for (x = 0; x < 6; x++) {
        /* Nearest sampling retains the font's deliberately clean pixel
         * contours instead of adding costly alpha blending to the browser. */
        int alpha = bitmap[(y * 2) * SUBTITLE_FONT_CELL_WIDTH * 16 + x * 2];
        if (bitmap[(y * 2) * SUBTITLE_FONT_CELL_WIDTH * 16 + x * 2 + 1] > alpha)
            alpha = bitmap[(y * 2) * SUBTITLE_FONT_CELL_WIDTH * 16 + x * 2 + 1];
        if (bitmap[(y * 2 + 1) * SUBTITLE_FONT_CELL_WIDTH * 16 + x * 2] > alpha)
            alpha = bitmap[(y * 2 + 1) * SUBTITLE_FONT_CELL_WIDTH * 16 + x * 2];
        if (bitmap[(y * 2 + 1) * SUBTITLE_FONT_CELL_WIDTH * 16 + x * 2 + 1] > alpha)
            alpha = bitmap[(y * 2 + 1) * SUBTITLE_FONT_CELL_WIDTH * 16 + x * 2 + 1];
        if (alpha > 100) {
            int px = left + x, py = top + y;
            if (px >= 0 && px < VIDEO_WIDTH && py >= 0 && py < VIDEO_HEIGHT)
                vram[py * VIDEO_STRIDE + px] = color;
        }
    }
}

static void gui_text(int left, int top, u32 color, const char *format, ...) {
    char line[256];
    const char *cursor;
    va_list arguments;
    int glyph, index = 0;
    u32 *vram = (u32 *)0x44000000;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    if (!subtitle_font) subtitle_load_font();
    if (!subtitle_font) return;
    cursor = line;
    while (*cursor && left + index * 7 < VIDEO_WIDTH - 6) {
        glyph = subtitle_utf8_char(&cursor);
        gui_draw_small_glyph(vram, glyph, left + index * 7, top, color);
        index++;
    }
}

/* Analogue VU ballistics: the coil/needle state approaches a peak in small
 * equal steps in either direction.  A fresh louder impulse takes over on the
 * next draw, while silence lets the needle settle naturally towards zero. */
static void vu_ballistics_step(void) {
    int target_left = vu_left, target_right = vu_right;
    int delta;
    /* A frozen last PCM peak is misleading once the DAC is paused, a stream
     * ends, or it drops out.  Preserve the analogue decay, but let it decay
     * towards an actual zero signal in all three cases. */
    if (!audio_running || !audio_start) target_left = target_right = 0;
    if (target_left < 0) target_left = 0;
    if (target_left > 100) target_left = 100;
    if (target_right < 0) target_right = 0;
    if (target_right > 100) target_right = 100;
    delta = target_left - vu_display_left;
    if (delta) vu_display_left += delta > 0 ? (delta + 3) / 4 : (delta - 3) / 4;
    delta = target_right - vu_display_right;
    if (delta) vu_display_right += delta > 0 ? (delta + 3) / 4 : (delta - 3) / 4;
}

/* Integer square root keeps the spectrum independent of libm and is cheap
 * beside one 104 ms audio DMA block. */
static int spectrum_root(unsigned long long value) {
    unsigned long long bit = 1ULL << 62, root = 0;
    while (bit > value) bit >>= 2;
    while (bit) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else root >>= 1;
        bit >>= 2;
    }
    return root > 32767 ? 32767 : (int)root;
}

/* Twelve Goertzel bins over 64 stereo frames: this is a genuine frequency
 * measurement, from low/mid content at the left to treble on the right.
 * Values are deliberately gain-biased for a lively PSP-sized display. */
static void spectrum_measure(const short *pcm) {
    static const short coefficient[SPECTRUM_BANDS] = {
        8151, 8035, 7839, 7568, 7225, 6811,
        5793, 4551, 2381, 0, -3134, -6332
    };
    int band, sample;
    for (band = 0; band < SPECTRUM_BANDS; band++) {
        long long previous = 0, before_previous = 0, current;
        unsigned long long energy;
        int level;
        for (sample = 0; sample < 64; sample++) {
            int mono = (pcm[sample * 2] + pcm[sample * 2 + 1]) >> 7;
            current = mono + ((long long)coefficient[band] * previous >> 12) - before_previous;
            before_previous = previous;
            previous = current;
        }
        energy = (unsigned long long)(previous * previous + before_previous * before_previous -
                 ((long long)coefficient[band] * previous * before_previous >> 12));
        level = spectrum_root(energy) / 30;
        spectrum_levels[band] = level > 100 ? 100 : level;
    }
}

static void menu_skin_load(void) {
    if (receiver_skin_end - receiver_skin == MENU_SKIN_BYTES) menu_skin = receiver_skin;
}

static void gui_skin_receiver(u32 *vram) {
    int x;
    static const u32 indicator_colors[] = {
        0x0000D8FF, 0x00FFB000, 0x00B070FF, 0x0000FFD0,
        0x00D000FF, 0x00FF9040
    };
    static const signed char indicator_x_offset[] = {2, 2, 0, -2, -4};
    static const unsigned char indicator_width_extra[] = {3, 2, 2, 3, 3};
    /* Marker follows the visible inner rim of the photorealistic knob, not
     * a tiny circle near its centre.  Every one of the 31 volume values has
     * a physical detent, so neighbouring values cannot jump between coarse
     * positions on the dial. */
    static const signed char knob_x[] = {0,4,8,11,15,18,21,24,25,25,23,20,16,11,6,0,-4,-9,-13,-16,-19,-22,-23,-25,-25,-25,-23,-22,-19,-16,-13};
    static const signed char knob_y[] = {-25,-25,-24,-22,-20,-18,-13,-8,-2,4,10,15,19,22,24,25,25,23,22,19,16,13,9,4,0,-4,-9,-13,-16,-19,-22};
    int pointer = playback_volume;
    static const signed char needle_x[] = {-25,-24,-22,-20,-18,-15,-12,-9,-6,-3,0,3,6,9,12,15,18,20,22,24,25};
    static const signed char needle_y[] = {-4,-7,-10,-12,-14,-16,-18,-19,-20,-21,-21,-21,-20,-19,-18,-16,-14,-12,-10,-7,-4};
    int needle, needle_right;
    vu_ballistics_step();
    needle = (vu_display_left * 20 + 50) / 100;
    needle_right = (vu_display_right * 20 + 50) / 100;
    /* Real moving needles over the printed analogue meter scales. */
    /* The generated artwork includes a neutral centre needle; erase just
     * that hairline before drawing the live coil position. */
    gui_line(vram, 68, 237, 68, 215, 0x00120F0B);
    gui_line(vram, 162, 237, 162, 215, 0x00120F0B);
    gui_line(vram, 68, 237, 68 + needle_x[needle], 237 + needle_y[needle], 0x0000B0FF);
    gui_line(vram, 162, 237, 162 + needle_x[needle_right], 237 + needle_y[needle_right], 0x0000B0FF);
    gui_rect(vram, 67, 236, 3, 3, 0x0000B0FF);
    gui_rect(vram, 161, 236, 3, 3, 0x0000B0FF);
    for (x = 0; x < 5; x++) {
        unsigned int state = (unsigned int)(sceKernelGetSystemTimeWide() / 600000ULL) ^ (unsigned int)(x * 0x45d9f3bU);
        u32 color;
        unsigned int mask = x == 0 ? PSP_CTRL_LTRIGGER : x == 1 ? PSP_CTRL_SELECT : x == 2 ? PSP_CTRL_RTRIGGER : 0;
        state ^= state >> 16;
        state *= 0x45d9f3bU;
        state ^= state >> 16;
        color = indicator_colors[state % (sizeof(indicator_colors) / sizeof(indicator_colors[0]))];
        /* These unused receiver indicators become slowly changing coloured
         * light bars.  A pressed mapped transport control still flashes
         * white over its own bar for immediate feedback. */
        if (mask && (receiver_flash_button & mask)) color = 0x00FFFFFF;
        gui_rect(vram, 221 + x * 31 + indicator_x_offset[x], 238,
                 22 + indicator_width_extra[x], 3, color);
    }
    /* Amber is the volume marker travelling around the knob's inner rim. */
    /* Rounded 5x5 LED: full centre, with the four corner pixels omitted. */
    gui_rect(vram, 420 + knob_x[pointer] - 1, 215 + knob_y[pointer] - 2, 3, 5, 0x0000D8FF);
    gui_rect(vram, 420 + knob_x[pointer] - 2, 215 + knob_y[pointer] - 1, 5, 3, 0x0000D8FF);
}

/* Fullscreen music retains the exact same physical receiver controls rather
 * than replacing them with a digital level bar. */
static void gui_audio_fullscreen_receiver(u32 *vram) {
    int y;
    menu_skin_load();
    if (!menu_skin) { receiver_hud(0); return; }
    for (y = 198; y < VIDEO_HEIGHT; y++)
        memcpy(vram + y * VIDEO_STRIDE, menu_skin + y * VIDEO_WIDTH * 4, VIDEO_WIDTH * 4);
    gui_skin_receiver(vram);
}

/* Receiver strip for the non-fullscreen video and upcoming audio mode.  It
 * deliberately uses primitives, so there is no additional texture memory. */
static void receiver_hud(int frames) {
    u32 *vram = (u32 *)0x44000000;
    int x, meter_left, meter_right, level, pointer_x, pointer_y;
    static const signed char knob_x[] = {0,2,3,4,6,7,8,10,10,10,9,8,6,4,2,0,-2,-4,-5,-6,-8,-9,-9,-10,-10,-10,-9,-8,-8,-6,-5};
    static const signed char knob_y[] = {-10,-10,-10,-9,-8,-7,-5,-3,-1,2,4,6,8,9,10,10,10,9,9,8,6,5,4,2,0,-2,-4,-5,-6,-8,-9};
    gui_rect(vram, 0, 220, VIDEO_WIDTH, 52, 0x0010151B);
    gui_rect(vram, 0, 220, VIDEO_WIDTH, 1, 0x00D8E8FF);
    /* Two compact stereo VU meters. */
    vu_ballistics_step();
    meter_left = vu_display_left; meter_right = vu_display_right;
    if (meter_left > 100) meter_left = 100;
    if (meter_right > 100) meter_right = 100;
    for (x = 0; x < 10; x++) {
        level = (x + 1) * 10;
        gui_rect(vram, 18 + x * 4, 262 - x * 2, 3, x * 2 + 2,
                 meter_left >= level ? (x > 7 ? 0x00FFB000 : 0x0000D8FF) : 0x00202B33);
        gui_rect(vram, 66 + x * 4, 262 - x * 2, 3, x * 2 + 2,
                 meter_right >= level ? (x > 7 ? 0x00FFB000 : 0x0000D8FF) : 0x00202B33);
    }
    /* Transport buttons; their amber state is driven by the actual input. */
    for (x = 0; x < 5; x++) {
        u32 color = 0x00313A43;
        unsigned int mask = x == 0 ? PSP_CTRL_LTRIGGER : x == 1 ? PSP_CTRL_SELECT : x == 2 ? PSP_CTRL_RTRIGGER : 0;
        if (mask && (receiver_flash_button & mask)) color = 0x00D88700;
        gui_rect(vram, 132 + x * 38, 234, 31, 27, color);
        gui_rect(vram, 134 + x * 38, 236, 27, 23, 0x001B222A);
    }
    /* Volume knob with one positional marker for every volume detent. */
    gui_rect(vram, 353, 228, 49, 38, 0x00252C33);
    gui_rect(vram, 358, 233, 39, 28, 0x005A6268);
    pointer_x = 377 + knob_x[playback_volume];
    pointer_y = 247 + knob_y[playback_volume];
    gui_rect(vram, pointer_x - 1, pointer_y - 1, 3, 3, 0x00FFB000);
    for (x = 0; x < 30; x++)
        gui_rect(vram, 414 + x * 2, 258 - (x < playback_volume ? 10 : 4), 1,
                 x < playback_volume ? 10 : 4, x < playback_volume ? 0x00FFB000 : 0x002B343C);
    /* Keep the verified progress signal visible in receiver mode too. */
    playback_hud(frames, playback_paused);
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

/* Superseded software/YUV path.  Firmware AVC writes straight to VRAM, so
 * compiling this code would only reserve a large staging framebuffer. */
#if 0
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
#endif

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
            bitmap_present((int)(stream_start_seconds * 20.1f) + hardware_decoder_frames, audio_media_id);
            if (video_fullscreen || !receiver_visible) playback_hud(hardware_decoder_frames, playback_paused);
            else receiver_hud(hardware_decoder_frames);
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
#if 0
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
#endif

static void audio_measure_pcm(const short *pcm, int frames) {
    int sample, left_peak = 0, right_peak = 0;
    for (sample = 0; sample < frames * 2; sample += 64) {
        int left = pcm[sample] < 0 ? -pcm[sample] : pcm[sample];
        int right = pcm[sample + 1] < 0 ? -pcm[sample + 1] : pcm[sample + 1];
        if (left > left_peak) left_peak = left;
        if (right > right_peak) right_peak = right;
    }
    vu_left = left_peak * 100 / 32767;
    vu_right = right_peak * 100 / 32767;
    spectrum_measure(pcm);
}

static void audio_queue_destroy(void) {
    if (audio_queue_free_sema >= 0) {
        sceKernelDeleteSema(audio_queue_free_sema);
        audio_queue_free_sema = -1;
    }
    if (audio_queue_ready_sema >= 0) {
        sceKernelDeleteSema(audio_queue_ready_sema);
        audio_queue_ready_sema = -1;
    }
}

static int audio_queue_create(void) {
    audio_queue_destroy();
    audio_queue_free_sema = sceKernelCreateSema("PSPStreamerAudioFree", 0,
                                                 AUDIO_QUEUE_BLOCKS, AUDIO_QUEUE_BLOCKS, NULL);
    if (audio_queue_free_sema < 0) return -1;
    audio_queue_ready_sema = sceKernelCreateSema("PSPStreamerAudioReady", 0,
                                                  0, AUDIO_QUEUE_BLOCKS, NULL);
    if (audio_queue_ready_sema < 0) { audio_queue_destroy(); return -1; }
    return 0;
}

/* Use a finite wait so stopping a stream or losing WLAN cannot strand either
 * worker forever inside a semaphore wait. */
static int audio_queue_wait(SceUID sema) {
    SceUInt timeout = 10000;
    int result = sceKernelWaitSema(sema, 1, &timeout);
    return result >= 0;
}

static int audio_output_thread(SceSize args, void *argp) {
    int channel, block, dac_samples = audio_dac_samples;
    const int block_bytes = dac_samples * 2 * (int)sizeof(short);
    (void)args; (void)argp;
    /* MP3 is decoded natively at 44.1 kHz.  Use the regular DAC channel at
     * that exact rate instead of sending it through the SRC mixer.  On this
     * PSP the SRC path leaves a small, periodic seam between DMA blocks that
     * is especially audible in speech as a "tok-tok" artefact. */
    sceAudioSRCChRelease();
    /* PMPlayer consistently owns channel 0 for PCM playback.  Reserving an
     * arbitrary channel leaves channel selection to other resident services
     * and made this application's timing needlessly variable. */
    channel = sceAudioChReserve(0, dac_samples, PSP_AUDIO_FORMAT_STEREO);
    if (channel < 0) { audio_state = -16; audio_running = 0; return 0; }
    while (1) {
        while (audio_running && (!audio_start || !audio_queue_primed)) {
            sceKernelDelayThread(1000);
        }
        if (!audio_start) break;
        if (!audio_queue_wait(audio_queue_ready_sema)) {
            if (!audio_running) break;
            continue;
        }
        if (!audio_clock_started) audio_clock_started = 1;
        block = audio_queue_read;
        audio_queue_read = (audio_queue_read + 1) % AUDIO_QUEUE_BLOCKS;
        /* The producer cannot reuse this slot until OutputBlocking returns. */
        sceKernelDcacheWritebackRange(audio_samples + block * AUDIO_BLOCK_SAMPLES * 2, block_bytes);
        if (sceAudioOutputBlocking(channel, PSP_AUDIO_VOLUME_MAX * playback_volume / 30,
                                   audio_samples + block * AUDIO_BLOCK_SAMPLES * 2) < 0) {
            audio_state = -20; audio_running = 0; break;
        }
        sceKernelSignalSema(audio_queue_free_sema, 1);
        audio_played_blocks++;
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

static void gui_library_shell(const char *section);

static int audio_thread(SceSize args, void *argp) {
    struct sockaddr_in server;
    char request[2048], header[4096], *body;
    int socket_fd = -1, header_size = 0, received, output_thread_id = -1;
    int have = 0, frame_size, result, initial_size, frames_in_block = 0;
    int write_slot_reserved = 0;
    const int block_bytes = audio_dac_samples * 2 * (int)sizeof(short);
    const int decoded_bytes = MP3_DECODE_SAMPLES * 2 * (int)sizeof(short);
    (void)args; (void)argp;
    audio_state = 10;
    memset(mp3_codec, 0, sizeof(mp3_codec));
    if (sceAudiocodecCheckNeedMem(mp3_codec, PSP_CODEC_MP3) < 0) { audio_state = -21; audio_running = 0; return 0; }
    /* The firmware codec's ME-side DMA touches whole cache lines; reserve a
     * rounded work area, not merely the nominal byte count it reports. */
    mp3_codec_work = memalign(64, (mp3_codec[4] + 63) & ~63UL);
    if (!mp3_codec_work) { audio_state = -22; audio_running = 0; return 0; }
    mp3_codec[3] = (unsigned long)mp3_codec_work;
    if (sceAudiocodecInit(mp3_codec, PSP_CODEC_MP3) < 0) { audio_state = -23; goto cleanup; }
    /* Stand-alone music has no meaningful language/subtitle selection.  Its
     * first (and normally only) audio stream is always the source. */
    snprintf(request, sizeof(request), "GET /api/transcode/%s?container=mp3&profile=%s&audio=0&audio_quality=%s&start=%d HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", audio_media_id, PSP_STREAMER_PROFILE, audio_quality_name(), stream_start_seconds, server_host);
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
    audio_queue_read = audio_queue_write = 0;
    audio_queue_primed = audio_blocks_published = 0;
    if (audio_queue_create() < 0) { audio_state = -25; audio_running = 0; goto cleanup; }
    /* PMPlayer's output worker runs at ordinary playback priority.  The DAC
     * call itself blocks, so a very high priority only steals time from MP3
     * decoding and network refill around a block boundary. */
    output_thread_id = sceKernelCreateThread("PSPStreamerDAC", audio_output_thread, 0x3D, 0x2000, 0, NULL);
    if (output_thread_id < 0) { audio_state = -16; audio_running = 0; goto cleanup; }
    audio_output_thread_id = output_thread_id;
    sceKernelStartThread(output_thread_id, 0, NULL);
    while (audio_running) {
        if (!write_slot_reserved) {
            if (!audio_queue_wait(audio_queue_free_sema)) continue;
            if (!audio_running) break;
            write_slot_reserved = 1;
        }
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
        mp3_codec[8] = (unsigned long)mp3_decode_pcm;
        mp3_codec[9] = decoded_bytes;
        sceKernelDcacheWritebackRange(mp3_input_buffer, frame_size);
        sceKernelDcacheWritebackInvalidateRange(mp3_decode_pcm, decoded_bytes);
        result = sceAudiocodecDecode(mp3_codec, PSP_CODEC_MP3);
        if (result < 0) { audio_state = -24; audio_running = 0; break; }
        sceKernelDcacheInvalidateRange(mp3_decode_pcm, decoded_bytes);
        memcpy(audio_samples + audio_queue_write * AUDIO_BLOCK_SAMPLES * 2 +
               frames_in_block * MP3_DECODE_SAMPLES * 2, mp3_decode_pcm, decoded_bytes);
        memmove(mp3_input_buffer, mp3_input_buffer + frame_size, have - frame_size);
        have -= frame_size;
        frames_in_block++;
        if (frames_in_block * MP3_DECODE_SAMPLES == audio_dac_samples) {
            sceKernelDcacheInvalidateRange(audio_samples + audio_queue_write * AUDIO_BLOCK_SAMPLES * 2, block_bytes);
            sceKernelDcacheWritebackRange(audio_samples + audio_queue_write * AUDIO_BLOCK_SAMPLES * 2, block_bytes);
            audio_measure_pcm(audio_samples + audio_queue_write * AUDIO_BLOCK_SAMPLES * 2, audio_dac_samples);
            audio_queue_write = (audio_queue_write + 1) % AUDIO_QUEUE_BLOCKS;
            sceKernelSignalSema(audio_queue_ready_sema, 1);
            frames_in_block = 0;
            write_slot_reserved = 0;
            audio_blocks_published++;
            if (audio_blocks_published >= audio_prefill_target) {
                audio_queue_primed = 1;
                audio_state = 15;
            }
        }
    }
cleanup:
    /* A decoder failure or end-of-stream must wake the UI and DAC worker.
     * Previously this flag could remain true after the producer had gone,
     * leaving the player apparently frozen with an empty audio queue. */
    audio_running = 0;
    audio_start = 1;
    if (write_slot_reserved && audio_queue_free_sema >= 0)
        sceKernelSignalSema(audio_queue_free_sema, 1);
    if (audio_socket_fd == socket_fd) { audio_socket_fd = -1; if (socket_fd >= 0) sceNetInetClose(socket_fd); }
    if (mp3_codec_work) { free(mp3_codec_work); mp3_codec_work = NULL; }
    return 0;
}

static int play_audio(const char *media_id, const char *title) {
    int audio_thread_id, paused = 0, fullscreen = 0, stopped_by_user = 0;
    unsigned int old = 0;
    unsigned long long next_volume_repeat_tick = 0;
    strncpy(audio_media_id, media_id, sizeof(audio_media_id) - 1);
    audio_media_id[sizeof(audio_media_id) - 1] = '\0';
    audio_queue_read = audio_queue_write = audio_played_blocks = 0;
    audio_queue_primed = audio_blocks_published = 0;
    vu_left = vu_right = vu_display_left = vu_display_right = 0;
    memset((void *)spectrum_levels, 0, sizeof(spectrum_levels));
    memset(spectrum_display, 0, sizeof(spectrum_display));
    audio_output_thread_id = -1;
    audio_prefill_target = AUDIO_MUSIC_PREFILL_BLOCKS;
    audio_dac_samples = AUDIO_BLOCK_SAMPLES;
    playback_reached_end = 0;
    audio_running = 1; audio_start = 1; audio_clock_started = 0; audio_state = 0;
    audio_thread_id = sceKernelCreateThread("PSPStreamerMusic", audio_thread, 0x18, 0x4000, 0, NULL);
    if (audio_thread_id < 0) { audio_running = 0; return audio_thread_id; }
    sceKernelStartThread(audio_thread_id, 0, NULL);
    while (audio_running) {
        SceCtrlData pad;
        int x;
        keep_awake();
        if (fullscreen) vu_ballistics_step();
        if (!fullscreen) {
            gui_library_shell(tr(TXT_NOW_PLAYING));
            gui_text(38, 40, 0x0000D8FF, "%s", tr(TXT_MUSIC_STREAM));
            gui_text(38, 52, 0x00FFFFFF, "%.39s", title);
            gui_text(38, 64, 0x008A9BAA, tr(TXT_VOLUME_LINE), playback_volume * 100 / 30);
        } else {
            gui_rect((u32 *)0x44000000, 0, 0, VIDEO_WIDTH, VIDEO_HEIGHT, 0x00080E14);
            gui_rect((u32 *)0x44000000, 0, 0, VIDEO_WIDTH, 2, 0x00D8E8FF);
            gui_text(18, 12, 0x00D8E8FF, tr(TXT_FULLSCREEN_MUSIC), title);
        }
        /* Actual PCM frequency bins, not a decorative level animation. */
        for (x = 0; x < SPECTRUM_BANDS; x++) {
            int target = (!audio_running || !audio_start) ? 0 : spectrum_levels[x];
            int height, baseline = fullscreen ? 194 : 150;
            u32 color = x < 4 ? 0x0000D8FF : x < 8 ? 0x00B070FF : 0x00FFB000;
            if (target > spectrum_display[x])
                spectrum_display[x] += (target - spectrum_display[x] + 1) / 2;
            else if (spectrum_display[x] > 3)
                spectrum_display[x] -= 3;
            else spectrum_display[x] = 0;
            height = spectrum_display[x] * (fullscreen ? 145 : 82) / 100;
            gui_rect((u32 *)0x44000000, fullscreen ? 24 + x * 36 : 42 + x * 23, baseline - height,
                     fullscreen ? 25 : 15, height, color);
        }
        if (fullscreen) gui_audio_fullscreen_receiver((u32 *)0x44000000);
        else gui_text(38, 177, 0x00FFFFFF, "%s", tr(TXT_MUSIC_CONTROLS));
        sceDisplaySetFrameBuf((void *)0x04000000, VIDEO_STRIDE, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTVSYNC);
        sceDisplayWaitVblankStart();
        sceCtrlPeekBufferPositive(&pad, 1);
        if ((pad.Buttons & PSP_CTRL_START) && !(old & PSP_CTRL_START)) {
            stopped_by_user = 1;
            break;
        }
        if ((pad.Buttons & PSP_CTRL_SELECT) && !(old & PSP_CTRL_SELECT)) { paused = !paused; audio_start = !paused; }
        if (pad.Buttons & (PSP_CTRL_UP | PSP_CTRL_DOWN)) {
            unsigned int direction = (pad.Buttons & PSP_CTRL_UP) ? PSP_CTRL_UP : PSP_CTRL_DOWN;
            unsigned long long now = sceKernelGetSystemTimeWide();
            if (!(old & direction) || now >= next_volume_repeat_tick) {
                if (direction == PSP_CTRL_UP && playback_volume < 30) playback_volume++;
                if (direction == PSP_CTRL_DOWN && playback_volume > 0) playback_volume--;
                save_playback_settings();
                /* First step is immediate; held input then moves at a calm
                 * five-and-a-half detents per second. */
                next_volume_repeat_tick = now + 180000;
            }
        } else next_volume_repeat_tick = 0;
        if ((pad.Buttons & (PSP_CTRL_CROSS | PSP_CTRL_TRIANGLE)) == (PSP_CTRL_CROSS | PSP_CTRL_TRIANGLE) &&
            (old & (PSP_CTRL_CROSS | PSP_CTRL_TRIANGLE)) != (PSP_CTRL_CROSS | PSP_CTRL_TRIANGLE)) fullscreen = !fullscreen;
        old = pad.Buttons;
        sceKernelDelayThread(33000);
    }
    audio_running = 0; audio_start = 1;
    if (audio_socket_fd >= 0) { int fd = audio_socket_fd; audio_socket_fd = -1; sceNetInetClose(fd); }
    sceKernelWaitThreadEnd(audio_thread_id, NULL);
    sceKernelDeleteThread(audio_thread_id);
    if (audio_output_thread_id >= 0) {
        int output_thread_id = audio_output_thread_id;
        sceKernelWaitThreadEnd(output_thread_id, NULL);
        sceKernelDeleteThread(output_thread_id);
        audio_output_thread_id = -1;
    }
    audio_queue_destroy();
    /* The MP3 worker deliberately treats HTTP EOF as a neutral shutdown so
     * transient WLAN failures do not masquerade as decoder faults.  Compare
     * the DAC clock to ffprobe's duration here to classify a genuine song
     * end, just as video uses its rendered-frame clock. */
    if (!stopped_by_user && current_duration_seconds > 0.0f && audio_state >= 15 &&
        (float)audio_played_blocks * (float)audio_dac_samples / (float)PSP_AUDIO_SAMPLE_RATE >= current_duration_seconds * 0.90f)
        playback_reached_end = 1;
    return audio_state < 0 ? audio_state : 0;
}

static int play_h264(const char *media_id) {
    struct sockaddr_in server;
    char request[2048], header[4096], receive_buffer[4096], *body;
    int socket_fd, header_size = 0, received, h264_size = 0, frames = 0, result, buffered = 0, wait;
    int audio_thread_id = -1;
    unsigned long long next_frame_tick = 0;
    unsigned long long last_packet_tick;
    unsigned long long next_volume_repeat_tick = 0;
    unsigned int previous_buttons = 0;
    int paused = 0;
    playback_reached_end = 0;
    playback_paused = 0;
    vu_left = vu_right = vu_display_left = vu_display_right = 0;
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
    audio_prefill_target = AUDIO_PREFILL_BLOCKS;
    audio_dac_samples = AUDIO_BLOCK_SAMPLES;
    audio_queue_primed = audio_blocks_published = 0;
    audio_thread_id = sceKernelCreateThread("PSPStreamerAudio", audio_thread,
                                            0x18, 0x4000, 0, NULL);
    if (audio_thread_id >= 0) sceKernelStartThread(audio_thread_id, 0, NULL);
    else { audio_running = 0; audio_state = audio_thread_id; }
    video_step = "TCP connection";
    snprintf(request, sizeof(request), "GET /api/transcode/%s?container=h264&profile=%s&audio=%d&subtitle=%d&start=%d HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", media_id, PSP_STREAMER_PROFILE, selected_audio_track, (subtitle_client_side || bitmap_client_side) ? -1 : selected_subtitle_track, stream_start_seconds, server_host);
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
            playback_paused = paused;
            audio_start = paused ? 0 : 1;
            playback_hud(hardware_decoder_frames, paused);
            sceDisplaySetFrameBuf((void *)0x04000000, VIDEO_STRIDE, PSP_DISPLAY_PIXEL_FORMAT_8888,
                                  PSP_DISPLAY_SETBUF_NEXTVSYNC);
        }
        if ((pad.Buttons & (PSP_CTRL_CROSS | PSP_CTRL_TRIANGLE)) == (PSP_CTRL_CROSS | PSP_CTRL_TRIANGLE) &&
            (previous_buttons & (PSP_CTRL_CROSS | PSP_CTRL_TRIANGLE)) != (PSP_CTRL_CROSS | PSP_CTRL_TRIANGLE)) {
            video_fullscreen = !video_fullscreen;
            receiver_flash_button = PSP_CTRL_TRIANGLE;
        }
        if ((pad.Buttons & PSP_CTRL_CIRCLE) && !(previous_buttons & PSP_CTRL_CIRCLE)) {
            receiver_visible = !receiver_visible;
            receiver_flash_button = PSP_CTRL_CIRCLE;
        }
        if (pad.Buttons & (PSP_CTRL_UP | PSP_CTRL_DOWN)) {
            unsigned int direction = (pad.Buttons & PSP_CTRL_UP) ? PSP_CTRL_UP : PSP_CTRL_DOWN;
            unsigned long long now = sceKernelGetSystemTimeWide();
            if (!(previous_buttons & direction) || now >= next_volume_repeat_tick) {
                if (direction == PSP_CTRL_UP && playback_volume < 30) playback_volume++;
                if (direction == PSP_CTRL_DOWN && playback_volume > 0) playback_volume--;
                receiver_flash_button = direction;
                save_playback_settings();
                next_volume_repeat_tick = now + 180000;
            }
        } else next_volume_repeat_tick = 0;
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
        if (!(pad.Buttons & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_SELECT | PSP_CTRL_CIRCLE | PSP_CTRL_TRIANGLE)))
            receiver_flash_button = 0;
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
    /* Wake a blocking receive before returning to the browser.  Otherwise
     * it retains the firmware MP3 handle and the next film is silent. */
    if (audio_socket_fd >= 0) {
        int closing_socket = audio_socket_fd;
        audio_socket_fd = -1;
        sceNetInetClose(closing_socket);
    }
    if (audio_thread_id >= 0) {
        sceKernelWaitThreadEnd(audio_thread_id, NULL);
        sceKernelDeleteThread(audio_thread_id);
    }
    if (audio_output_thread_id >= 0) {
        int output_thread_id = audio_output_thread_id;
        sceKernelWaitThreadEnd(output_thread_id, NULL);
        sceKernelDeleteThread(output_thread_id);
        audio_output_thread_id = -1;
    }
    audio_queue_destroy();
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
        if (!json_value(object, "t", tracks[*count].title, sizeof(tracks[*count].title)))
            tracks[*count].title[0] = '\0';
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
    /* Do not prefetch subtitle payloads here.  In particular, a first PGS
     * selection may require mkvextract to build its persistent server cache,
     * which is legitimate work but can take tens of seconds on an SMB disk.
     * Keeping this phase metadata-only guarantees that the options dialog
     * stays responsive; play_h264() prepares the selected overlay only once
     * the user has actually committed to starting the video. */
}

static void gui_library_shell(const char *section);

static void show_metadata_loading(void) {
    gui_library_shell(tr(TXT_PREPARING_MEDIA));
    gui_text(38, 47, 0x0000D8FF, "%s", tr(TXT_READING_MEDIA));
    gui_text(38, 76, 0x00FFFFFF, "%s", tr(TXT_LOADING_TRACKS));
    gui_text(38, 96, 0x008A9BAA, "%s", tr(TXT_SOURCE_WAKING));
    /* Start at the panel edge; the compact glyph itself already carries its
     * own tiny left bearing, so an extra character-cell offset reads as a
     * spurious leading blank on the PSP LCD. */
    gui_text(369, 47, 0x00FFB000, "%s", tr(TXT_PLEASE_WAIT));
    gui_text(376, 76, 0x008A9BAA, "%s", tr(TXT_NO_VIDEO));
    gui_text(376, 87, 0x008A9BAA, "%s", tr(TXT_STREAM_HAS));
    gui_text(376, 98, 0x008A9BAA, "%s", tr(TXT_STARTED));
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
        { char kind[12]; items[item_count].is_audio = json_value(object, "kind", kind, sizeof(kind)) && !strcmp(kind, "audio"); }
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

/* Continue within the same kind of media.  A music album must never spill
 * into a video merely because both happen to share a folder. */
static int next_media_index(int selected, int is_audio) {
    int index, candidates = 0, wanted;
    static unsigned int shuffle_state;
    if (is_audio && audio_shuffle) {
        for (index = 0; index < item_count; index++)
            if (index != selected && !items[index].is_folder && items[index].is_audio) candidates++;
        if (!candidates) return -1;
        if (!shuffle_state) shuffle_state = (unsigned int)sceKernelGetSystemTimeWide() | 1U;
        shuffle_state = shuffle_state * 1103515245U + 12345U;
        wanted = (shuffle_state >> 8) % candidates;
        for (index = 0; index < item_count; index++)
            if (index != selected && !items[index].is_folder && items[index].is_audio && wanted-- == 0) return index;
        return -1;
    }
    for (index = selected + 1; index < item_count; index++)
        if (!items[index].is_folder && items[index].is_audio == is_audio) return index;
    return -1;
}

static void refresh_library(void) {
    int result;
    char encoded_path[ID_SIZE * 3 + 1];
    char api_path[ID_SIZE * 3 + 32];
    if (!network_ready || !http_ready) {
        strcpy(status, tr(TXT_WIFI_NOT_READY));
        return;
    }
    strcpy(status, tr(TXT_LOADING_LIBRARY));
    url_encode(current_path, encoded_path, sizeof(encoded_path));
    snprintf(api_path, sizeof(api_path), "/api/library?path=%s", encoded_path);
    result = http_get(api_path, response, sizeof(response));
    if (result < 0) {
        snprintf(status, sizeof(status), tr(TXT_SERVER_ERROR), result);
        /* A just-restored hotspot often has IP before DNS.  Keep the useful
         * browser state visible so Square can simply be tried again. */
        return;
    }
    parse_library();
    snprintf(status, sizeof(status), tr(TXT_ENTRIES), item_count);
}

/* The library is intentionally drawn with the same inexpensive VRAM
 * primitives as the receiver strip.  It replaces the old diagnostic-console
 * landing page.  No video decoder buffers or textures are involved here. */
static void gui_library_shell(const char *section) {
    u32 *vram = (u32 *)0x44000000;
    int x, lit_left = vu_left / 10, lit_right = vu_right / 10;
    int y;
    menu_skin_load();
    if (menu_skin) {
        for (y = 0; y < VIDEO_HEIGHT; y++)
            memcpy(vram + y * VIDEO_STRIDE, menu_skin + y * VIDEO_WIDTH * 4, VIDEO_WIDTH * 4);
        gui_skin_receiver(vram);
        gui_text(27, 11, 0x00FFFFFF, "PSP STREAMER // %s", section);
        return;
    }
    gui_rect(vram, 0, 0, VIDEO_WIDTH, VIDEO_HEIGHT, 0x00080E14);
    gui_rect(vram, 0, 0, VIDEO_WIDTH, 27, 0x00141C25);
    gui_rect(vram, 0, 26, VIDEO_WIDTH, 1, 0x00D8E8FF);
    gui_rect(vram, 8, 37, 298, 174, 0x00131A22);
    gui_rect(vram, 8, 37, 298, 1, 0x004B5B68);
    gui_rect(vram, 8, 37, 1, 174, 0x004B5B68);
    gui_rect(vram, 315, 37, 157, 174, 0x0010161D);
    gui_rect(vram, 315, 37, 157, 1, 0x004B5B68);
    gui_rect(vram, 315, 37, 1, 174, 0x004B5B68);
    /* Always-visible miniature receiver: it makes the application feel like
     * a media appliance before the first file is selected. */
    gui_rect(vram, 8, 226, 464, 38, 0x0010151B);
    gui_rect(vram, 8, 226, 464, 1, 0x00D8E8FF);
    for (x = 0; x < 10; x++) {
        gui_rect(vram, 23 + x * 5, 258 - x * 2, 3, x * 2 + 2,
                 x < lit_left ? (x > 7 ? 0x00FFB000 : 0x0000D8FF) : 0x00202B33);
        gui_rect(vram, 78 + x * 5, 258 - x * 2, 3, x * 2 + 2,
                 x < lit_right ? (x > 7 ? 0x00FFB000 : 0x0000D8FF) : 0x00202B33);
    }
    for (x = 0; x < 5; x++) {
        gui_rect(vram, 150 + x * 35, 238, 28, 21, 0x00313A43);
        gui_rect(vram, 152 + x * 35, 240, 24, 17, 0x001B222A);
    }
    gui_rect(vram, 353, 232, 43, 29, 0x005A6268);
    gui_rect(vram, 357, 236, 35, 21, 0x00252C33);
    gui_rect(vram, 373 + (playback_volume * 13 / 30), 240, 2, 13, 0x00FFB000);
    for (x = 0; x < 20; x++)
        gui_rect(vram, 414 + x * 2, 258 - (x < playback_volume * 2 / 3 ? 8 : 3), 1,
                 x < playback_volume * 2 / 3 ? 8 : 3,
                 x < playback_volume * 2 / 3 ? 0x00FFB000 : 0x002B343C);
    gui_text(18, 9, 0x00D8E8FF, "PSP STREAMER   //   %s", section);
}

static void show(int selected) {
    int i, first, last;
    if (selected < 0 || selected >= item_count) selected = 0;
    first = item_count ? (selected / GUI_LIST_ROWS) * GUI_LIST_ROWS : 0;
    last = first + GUI_LIST_ROWS;
    if (last > item_count) last = item_count;
    gui_library_shell(tr(TXT_MEDIA_LIBRARY));
    gui_text(38, 40, 0x0000D8FF, "%s", tr(TXT_LIBRARY));
    gui_text(38, 51, 0x008A9BAA, "%.39s", current_path[0] ? current_path : "/");
    if (!item_count) {
        gui_text(38, 80, 0x00FFFFFF, "%s", tr(TXT_NO_ENTRIES));
    } else {
        for (i = first; i < last; i++) {
            int row = i - first;
            if (i == selected) gui_rect((u32 *)0x44000000, 36, 64 + row * 8, 310, 8, 0x004A5A32);
            gui_text(38, 64 + row * 8, i == selected ? 0x00FFFFFF : 0x00D8E8FF,
                     "%c %.38s", items[i].is_folder ? '+' : (items[i].is_audio ? '~' : '>'), items[i].title);
        }
    }
    gui_text(376, 40, 0x00FFB000, "%s", tr(TXT_SELECTED));
    if (item_count) gui_text(376, 57, 0x00FFFFFF, "%.11s", items[selected].title);
    else gui_text(376, 57, 0x00FFFFFF, "%s", tr(TXT_WAITING));
    if (item_count) gui_text(376, 76, 0x008A9BAA, "%s", items[selected].is_folder ? tr(TXT_FOLDER) : (items[selected].is_audio ? tr(TXT_MUSIC) : tr(TXT_VIDEO)));
    gui_text(376, 90, 0x008A9BAA, tr(TXT_ENTRIES), item_count);
    gui_text(376, 104, 0x008A9BAA, tr(TXT_PROFILE), active_network_profile);
    if (hardware_runtime_result == 0) gui_text(376, 118, 0x008A9BAA, "%s", tr(TXT_AVC_READY));
    else if (hardware_runtime_result != -9999) gui_text(376, 118, 0x008A9BAA, "%s", tr(TXT_AVC_ERROR));
    gui_text(376, 138, 0x00FFFFFF, "%.11s", status);
    /* The tiny receiver sidebar intentionally clips ordinary status copy.
     * Decoder diagnostics need their complete signed hex code, however. */
    if (!strncmp(status, "MP3 ", 4)) gui_text(38, 160, 0x00FFB000, "%s", status);
    gui_text(38, 177, 0x00FFFFFF, "%s", tr(TXT_LIBRARY_CONTROLS));
}

/* A real media-information screen rather than a second copy of the browser.
 * Metadata is deliberately the compact server response already used by the
 * playback setup, so opening this page cannot start a subtitle conversion or
 * disturb the proven H.264/MP3 pipeline. */
static void media_info(int selected) {
    SceCtrlData pad;
    unsigned int old = 0;
    int i, minutes = (int)current_duration_seconds / 60;
    int seconds = (int)current_duration_seconds % 60;
    do {
        sceCtrlReadBufferPositive(&pad, 1);
        sceKernelDelayThread(10000);
    } while (pad.Buttons & PSP_CTRL_TRIANGLE);
    while (1) {
        gui_library_shell(tr(TXT_FILE_DETAILS));
        gui_text(38, 40, 0x0000D8FF, "%s", tr(TXT_FILE_DETAILS));
        gui_text(38, 57, 0x00FFFFFF, "%.39s", items[selected].title);
        gui_text(38, 80, 0x008A9BAA, "TYPE: %s", items[selected].is_audio ? tr(TXT_MUSIC_STREAM) : tr(TXT_VIDEO_STREAM));
        if (current_duration_seconds > 0.0f)
            gui_text(38, 96, 0x008A9BAA, tr(TXT_DURATION), minutes, seconds);
        else gui_text(38, 96, 0x008A9BAA, "%s", tr(TXT_DURATION_UNKNOWN));
        gui_text(38, 112, 0x008A9BAA, tr(TXT_AUDIO_TRACKS), audio_track_count);
        gui_text(38, 128, 0x008A9BAA, tr(TXT_SUBTITLE_TRACKS), subtitle_track_count);
        gui_text(376, 40, 0x00FFB000, "%s", tr(TXT_STREAMS));
        if (!audio_track_count && !subtitle_track_count) gui_text(376, 57, 0x008A9BAA, "%s", tr(TXT_NO_TRACKS));
        for (i = 0; i < audio_track_count && i < 6; i++)
            gui_text(376, 57 + i * 10, 0x00FFFFFF, "A%d %.10s", i + 1, audio_tracks[i].language);
        for (i = 0; i < subtitle_track_count && i + audio_track_count < 10; i++)
            gui_text(376, 57 + (i + audio_track_count) * 10, 0x008A9BAA, "S%d %.10s", i + 1, subtitle_tracks[i].language);
        gui_text(38, 177, 0x00FFFFFF, "%s", tr(TXT_INFO_CONTROLS));
        sceCtrlReadBufferPositive(&pad, 1);
        if ((pad.Buttons & (PSP_CTRL_CIRCLE | PSP_CTRL_TRIANGLE)) &&
            !(old & (PSP_CTRL_CIRCLE | PSP_CTRL_TRIANGLE))) return;
        if ((pad.Buttons & PSP_CTRL_CROSS) && !(old & PSP_CTRL_CROSS)) return;
        old = pad.Buttons;
        sceKernelDelayThread(75000);
    }
}

/* A compact pre-playback dialog.  Track numbers follow ffprobe/ffmpeg's
 * stream order; a later metadata pass can attach language labels without
 * changing the streaming protocol. */
static int playback_options(int audio_only) {
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
        gui_library_shell(tr(TXT_STREAM_OPTIONS));
        gui_text(38, 40, 0x0000D8FF, "%s", tr(TXT_STREAM_OPTIONS));
        if (audio_only) {
            if (row == 0) gui_rect((u32 *)0x44000000, 36, 64, 310, 9, 0x004A5A32);
            if (row == 1) gui_rect((u32 *)0x44000000, 36, 84, 310, 9, 0x004A5A32);
            gui_text(38, 64, 0x00FFFFFF, "%s: %s", tr(TXT_QUALITY), audio_quality_name());
            gui_text(38, 84, 0x00FFFFFF, "%s: %s", tr(TXT_PLAY_ORDER), tr(audio_shuffle ? TXT_SHUFFLE : TXT_SEQUENTIAL));
            gui_text(376, 47, 0x00FFB000, "%s", tr(TXT_QUALITY));
            gui_text(376, 76, 0x008A9BAA, "%s", tr(TXT_SAVED_FOR));
            gui_text(376, 87, 0x008A9BAA, "%s", tr(TXT_NEXT_MUSIC));
            gui_text(376, 98, 0x008A9BAA, "%s", tr(TXT_STREAM_BANG));
            gui_text(38, 177, 0x00FFFFFF, "%s", tr(TXT_MUSIC_SETUP_CONTROLS));
        } else {
            if (row == 0) gui_rect((u32 *)0x44000000, 36, 64, 310, 9, 0x004A5A32);
            if (row == 1) gui_rect((u32 *)0x44000000, 36, 84, 310, 9, 0x004A5A32);
            if (row == 2) gui_rect((u32 *)0x44000000, 36, 104, 310, 9, 0x004A5A32);
            gui_text(38, 64, 0x00FFFFFF, tr(TXT_AUDIO_LABEL),
                                 audio_track_count ? audio_tracks[selected_audio_track].language : tr(TXT_NOT_DETECTED),
                                 audio_track_count && audio_tracks[selected_audio_track].title[0] ? " - " : "",
                                 audio_track_count ? audio_tracks[selected_audio_track].title : "");
            gui_text(38, 84, 0x00FFFFFF, tr(TXT_SUBS_LABEL),
                                 selected_subtitle_track < 0 ? tr(TXT_OFF) : subtitle_tracks[selected_subtitle_track].language,
                                 selected_subtitle_track >= 0 && subtitle_tracks[selected_subtitle_track].title[0] ? " - " : "",
                                 selected_subtitle_track >= 0 ? subtitle_tracks[selected_subtitle_track].title : "");
            gui_text(38, 104, 0x00FFFFFF, "%s: %s", tr(TXT_QUALITY), audio_quality_name());
            gui_text(373, 47, 0x00FFB000, "%s", tr(TXT_AUDIO_SUB));
            gui_text(376, 76, 0x008A9BAA, "%s", tr(TXT_SAVED_FOR));
            gui_text(376, 87, 0x008A9BAA, "%s", tr(TXT_NEXT_PLAY));
            gui_text(376, 98, 0x008A9BAA, "%s", tr(TXT_BACK));
            gui_text(38, 177, 0x00FFFFFF, "%s", tr(TXT_VIDEO_SETUP_CONTROLS));
        }
        sceCtrlReadBufferPositive(&pad, 1);
        if ((pad.Buttons & PSP_CTRL_CIRCLE) && !(old & PSP_CTRL_CIRCLE)) return 0;
        if ((pad.Buttons & PSP_CTRL_CROSS) && !(old & PSP_CTRL_CROSS)) return 1;
        if (audio_only && (pad.Buttons & PSP_CTRL_UP) && !(old & PSP_CTRL_UP)) row = (row + 1) % 2;
        if (audio_only && (pad.Buttons & PSP_CTRL_DOWN) && !(old & PSP_CTRL_DOWN)) row = (row + 1) % 2;
        if (!audio_only && (pad.Buttons & PSP_CTRL_UP) && !(old & PSP_CTRL_UP)) row = (row + 2) % 3;
        if (!audio_only && (pad.Buttons & PSP_CTRL_DOWN) && !(old & PSP_CTRL_DOWN)) row = (row + 1) % 3;
        if ((pad.Buttons & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) && !(old & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT))) {
            int delta = (pad.Buttons & PSP_CTRL_RIGHT) ? 1 : -1;
            if (audio_only && row == 0) selected_audio_quality = (selected_audio_quality + delta + 3) % 3;
            else if (audio_only) audio_shuffle = !audio_shuffle;
            else if (row == 0 && audio_track_count)
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
    unsigned long long next_page_repeat_tick = 0;
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
    strcpy(status, tr(TXT_CONNECTING_WIFI));
    /* Present the media appliance immediately.  Network association can take
     * several seconds on a PSP; leaving the old blank debug screen there made
     * the application appear to start only after a file was chosen. */
    show(0);
    result = wait_for_network();
    if (result < 0) {
        snprintf(status, sizeof(status), tr(TXT_NETWORK_FAILED), failure_step, result);
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
        if (item_count && (pad.Buttons & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER))) {
            unsigned long long now = sceKernelGetSystemTimeWide();
            unsigned int trigger = pad.Buttons & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER);
            if (!(old_buttons & trigger) || now >= next_page_repeat_tick) {
                if (trigger & PSP_CTRL_RTRIGGER) selected = (selected + GUI_LIST_ROWS) % item_count;
                else selected = (selected + item_count - (GUI_LIST_ROWS % item_count)) % item_count;
                next_page_repeat_tick = now + 150000ULL;
                dirty = 1;
            }
        } else next_page_repeat_tick = 0;
        if ((pad.Buttons & PSP_CTRL_LEFT) && !(old_buttons & PSP_CTRL_LEFT) && current_path[0]) { parent_path(); selected = 0; refresh_library(); dirty = 1; }
        if (item_count && (pad.Buttons & PSP_CTRL_TRIANGLE) && !(old_buttons & PSP_CTRL_TRIANGLE) && !items[selected].is_folder) {
            /* Triangle is deliberately information-only: it performs the
             * same lightweight metadata request as X, but never begins a
             * transcode or subtitle preparation. */
            show_metadata_loading();
            load_media_metadata(items[selected].value);
            media_info(selected);
            dirty = 1;
            old_buttons = pad.Buttons;
            continue;
        }
        if (item_count && (pad.Buttons & PSP_CTRL_CROSS) && !(old_buttons & PSP_CTRL_CROSS) && items[selected].is_folder) {
            strncpy(current_path, items[selected].value, sizeof(current_path) - 1);
            current_path[sizeof(current_path) - 1] = '\0';
            selected = 0;
            refresh_library();
            dirty = 1;
        } else if (item_count && (pad.Buttons & PSP_CTRL_CROSS) && !(old_buttons & PSP_CTRL_CROSS) && !items[selected].is_folder) {
            if (resume_pending && !strcmp(resume_media_id, items[selected].value)) {
                snprintf(status, sizeof(status), tr(TXT_RESUMING), stream_start_seconds);
                show(selected);
                if (wait_for_network_restore() < 0) {
                    strcpy(status, tr(TXT_WIFI_RETRY));
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
                if (!playback_options(items[selected].is_audio)) { dirty = 1; old_buttons = pad.Buttons; continue; }
            }
            do {
                int next;
                snprintf(status, sizeof(status), "%s", items[selected].is_audio ? tr(TXT_STARTING_MUSIC) : tr(TXT_STARTING_VIDEO));
                show(selected);
                result = items[selected].is_audio ? play_audio(items[selected].value, items[selected].title) : play_h264(items[selected].value);
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
                    snprintf(status, sizeof(status), "%s", tr(TXT_INTERRUPTED));
                    break;
                }
                resume_pending = 0;
                next = playback_reached_end ? next_media_index(selected, items[selected].is_audio) : -1;
                if (next < 0) {
                    if (items[selected].is_audio)
                        snprintf(status, sizeof(status), tr(TXT_MUSIC_ENDED), audio_state);
                    else
                        snprintf(status, sizeof(status), tr(TXT_VIDEO_ENDED), result);
                    break;
                }
                selected = next;
                resume_pending = 0;
                stream_start_seconds = 0;
                snprintf(status, sizeof(status), items[selected].is_audio ? tr(TXT_NEXT_TRACK) : tr(TXT_NEXT_EPISODE), items[selected].title);
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
