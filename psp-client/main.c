#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspnet.h>
#include <pspnet_apctl.h>
#include <pspnet_inet.h>
#include <pspnet_resolver.h>
#include <pspdisplay.h>
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
#include "theme_layout.h"
static int theme_text_active;
#include "milkdrop_warp.h"
#include "h264_hw.h"
#include "language.h"
#include "display_output.h"
#include "tv_canvas.h"
#include "help_pages.h"
#include "power_policy.h"
#include "exit_policy.h"
#include "remote_input.h"
static void help_open(int topic);

PSP_MODULE_INFO("PSPStreamer", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(256);
/* newlib otherwise claims the largest user-memory block minus only 512 KiB.
 * Thread stacks and firmware modules cannot allocate from that malloc heap.
 * Keep headroom for TLS reader/remote stacks and the media firmware instead. */
PSP_HEAP_THRESHOLD_SIZE_KB(2048);

#define RESPONSE_SIZE (512 * 1024)
/* The PSP-3000 has 64 MiB RAM.  Keep a generously sized, paged directory
 * index so large anime/series roots are browsable instead of silently cut at
 * 24 entries. */
#define MAX_ITEMS 1024
#define LIST_ROWS 20
#define GUI_LIST_ROWS 11
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

/* Subtitle cues use source milliseconds, like the container presentation
 * timestamps. No frame-rate conversion is involved. */
#define MAX_SUBTITLE_CUES 960
#define SUBTITLE_TEXT_SIZE 160
#define SUBTITLE_FONT_CELL_WIDTH 16
#define SUBTITLE_FONT_CELL_HEIGHT 20
#define SUBTITLE_FONT_BYTES (16 * 16 * SUBTITLE_FONT_CELL_WIDTH * SUBTITLE_FONT_CELL_HEIGHT)
typedef struct {
    int start_ms;
    int end_ms;
    char text[SUBTITLE_TEXT_SIZE];
} SubtitleCue;
#include "subtitle_pages.h"
typedef struct { int start, end, x, y, width, height, canvas_width, canvas_height; } BitmapCue;

static char response[RESPONSE_SIZE];
/* Both native menu skins are embedded; no external artwork file is needed. */
extern unsigned char receiver_skin[];
extern unsigned char receiver_skin_end[];
static const unsigned char *menu_skin;
/* 512 pixels is the required power-of-two display stride. */
static LibraryItem items[MAX_ITEMS];
static int item_count;
static char current_path[ID_SIZE];
static char current_parent_path[ID_SIZE];
static int network_ready;
static int http_ready;
static int active_network_profile;
static const char *failure_step = "Network";
static const char *video_step = "Start";
static volatile int audio_running;
static volatile int audio_start;
static volatile int audio_clock_started;
/* Video releases the first audio block after its first prepared frame reaches
 * PSP scanout. This barrier does not measure downstream TV processing time.
 * Stand-alone music bypasses it. */
static volatile int video_first_presented;
static volatile int audio_socket_fd = -1;
static volatile int audio_output_thread_id = -1;
/* Kept deliberately numeric: it is displayed after START exits playback and
 * identifies the exact network/audio stage on real hardware. */
static volatile int audio_state;
/* Playback position and synchronisation use container milliseconds. */
static int playback_position_ms;
/* Overlay helpers target the off-screen frame only during preparation. */
static u32 *playback_draw_target = (u32 *)0x44000000;
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
static void keep_awake(void);
static int remote_control_thread(SceSize args, void *argp);

extern int pspDveMgrCheckVideoOut(void);
extern int pspDveMgrSetVideoOut(int unknown, int mode, int width, int height,
                                int x, int y, int flags);

static int tvout_module_id = -1;
static int tvout_video_active;
static int tv_ui_auto;
static int tv_ui_active;
static TvCanvas tv_canvas;
static void tv_ui_start(void);
static void ui_restore_after_playback(void);
static volatile int remote_control_running;
static volatile int remote_control_action;
static volatile int remote_control_seek_seconds = -1;
static int remote_control_thread_id = -1;
static int remote_control_sequence;
static int music_transition;
static int app_exit_requested;
static void music_transition_end(void);
#define TVOUT_STRIDE 768

static int output_mode(int tv) {
    return pspDveMgrSetVideoOut(0, tv ? 0x1d2 : 0, tv ? 720 : 480,
                              tv ? 480 : 272, 1, 15, 0);
}
static int output_framebuffer(int tv) {
    sceDisplayWaitVblankStart();
    return sceDisplaySetFrameBuf((void *)0x04000000, tv ? TVOUT_STRIDE : VIDEO_STRIDE,
                                PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTVSYNC);
}
static DisplayOutput display_output = {0, output_mode, output_framebuffer};

static int tvout_load_manager(void) {
    char path[256], cwd[192];
    int status;
    if (tvout_module_id >= 0) return 0;
    if (!getcwd(cwd, sizeof(cwd))) strncpy(cwd, PSP_STREAMER_INSTALL_DIR, sizeof(cwd) - 1);
    cwd[sizeof(cwd) - 1] = '\0';
    snprintf(path, sizeof(path), "%s/dvemgr.prx", cwd);
    tvout_module_id = kuKernelLoadModule(path, 0, NULL);
    if (tvout_module_id < 0) return tvout_module_id;
    status = 0;
    return sceKernelStartModule(tvout_module_id, 0, NULL, &status, NULL);
}

/* Isolated component-480p calibration.  This intentionally uses the PSP's
 * real 2 MiB EDRAM (the same address space that sceDisplaySetFrameBuf scans)
 * rather than the 0x0A extra-RAM window.  The latter silently fell back to
 * the old 512-pixel LCD scanout on this ARK-5 PSP-3000. */
static int tvout_component_test(void) {
    SceCtrlData pad;
    unsigned int old = 0;
    u32 *vram;
    int cable, x, y, result;
    result = tvout_load_manager();
    if (result < 0) return result;
    cable = pspDveMgrCheckVideoOut();
    if (cable != 2) return cable ? -2 : -1;
    result = display_output_select(&display_output, 1);
    if (result < 0) return result;
    vram = (u32 *)0x44000000;
    for (y = 0; y < 480; y++) for (x = 0; x < 720; x++) {
        u32 color = x < 180 ? 0x000000ff : x < 360 ? 0x0000ff00 : x < 540 ? 0x00ff0000 : 0x00ffffff;
        if ((y / 24) & 1) color >>= 1;
        vram[y * TVOUT_STRIDE + x] = color;
    }
    sceDisplayWaitVblankStart();
    sceDisplaySetFrameBuf(vram, TVOUT_STRIDE, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTVSYNC);
    while (1) {
        keep_awake();
        sceCtrlReadBufferPositive(&pad, 1);
        if ((pad.Buttons & (PSP_CTRL_SELECT | PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) ==
            (PSP_CTRL_SELECT | PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER) &&
            (old & (PSP_CTRL_SELECT | PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) !=
            (PSP_CTRL_SELECT | PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) break;
        old = pad.Buttons;
        sceKernelDelayThread(20000);
    }
    display_output_select(&display_output, tv_ui_active);
    return 0;
}

/* Video uses the exact EDRAM layout proven by the calibration card.
 * Native TV menus already own this mode; the default LCD menu enters it
 * only for playback. Decoder initialisation is independent of this choice. */
static int tvout_begin_video(void) {
    int result;
    result = tvout_load_manager();
    if (result < 0 || pspDveMgrCheckVideoOut() != 2) {
        /* A removed cable must not leave LCD video in a TV-stride scanout. */
        tv_ui_active = 0;
        if (display_output.tv) display_output_select(&display_output, 0);
        return -1;
    }
    return display_output_select(&display_output, 1);
}

static void tvout_end_video(void) {
    display_output_select(&display_output, tv_ui_active);
}

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
/* MPEG-1 Layer III, 320 kbit/s at 44.1 kHz, including padding. */
#define MP3_MAX_FRAME_BYTES 1045
/* Producer/consumer queue: network jitter is absorbed here while the output
 * thread feeds the DSP on time. */
static short audio_samples[AUDIO_BLOCK_SAMPLES * 2 * AUDIO_QUEUE_BLOCKS] __attribute__((aligned(64)));
/* Direct firmware codec input and its required work area.  Unlike sceMp3,
 * this path has no fake file offsets or opaque streaming ring. */
static unsigned char mp3_input_buffer[MP3_INPUT_BUFFER_BYTES] __attribute__((aligned(64)));
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
/* This mirrors PMPlayer Advance's output_audio_frame_buffers[].timestamp:
 * each decoded PCM ring slot carries its media timestamp, and the DAC worker
 * publishes that timestamp immediately before sceAudioOutputBlocking(). */
static unsigned int audio_block_timestamp_ms[AUDIO_QUEUE_BLOCKS];
static volatile unsigned int audio_current_timestamp_ms;
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
static int selected_video_fps;
static char music_preset_file[256]="active.milk";
static int music_preset_auto=0,music_preset_seconds=60,music_preset_fade_ms=1500;
#include "visual_options.h"
#include "preset_catalog.h"
#include "preset_sequence.h"
static int audio_shuffle;
/* 0..30 maps cleanly to the 30 LED detents in the receiver UI. */
static int playback_volume = 24;
static StreamTrack audio_tracks[8], subtitle_tracks[8];
static int audio_track_count, subtitle_track_count;
static SubtitleCue *subtitle_cues;
static int subtitle_cue_count;
static SubtitlePage *subtitle_pages;
static int subtitle_page_bank, subtitle_paged_response;
static volatile int subtitle_pages_live, subtitle_page_ready, subtitle_page_position;
static volatile int subtitle_page_failures;
static unsigned char *subtitle_font;
static BitmapCue *bitmap_cues;
static int bitmap_cue_count, bitmap_client_side, bitmap_loaded_cue = -1, bitmap_bytes;
static int subtitle_client_side;
static float current_duration_seconds;
static char current_media_name[TITLE_SIZE], current_media_title[192];
static char current_media_artist[192], current_media_album[192];
static int current_media_plex;
static int playback_reached_end;
static volatile int playback_paused;
static int video_fullscreen = 1;
static int receiver_visible;
static unsigned int receiver_flash_button;
static int stream_start_seconds;
static int download_before_play;
/* Opt-in diagnostics; normal errors and recovery remain active. */
static int debug_enabled;
static int resume_pending;
static char resume_media_id[ID_SIZE];
static int seek_requested;
#define SETTINGS_PATH "ms0:/PSP/SYSTEM/PSPStreamer.cfg"
static char server_host[64] = PSP_STREAMER_HOST;
static int server_https;
static int server_port = PSP_STREAMER_PORT;
#include "server_auth.h"
static struct in_addr cached_server_address;
static int have_cached_server_address;

#include "comfort_store.h"
#define COMFORT_PATH "ms0:/PSP/SYSTEM/PSPStreamer.state"
static ComfortStore comfort_store;
static LibraryItem comfort_focus,comfort_shortcut;
static int comfort_shortcut_pending,provider_resume_seconds;
static unsigned long long comfort_deadline;
static int comfort_remaining,comfort_timer_stopped;
static int comfort_menu(void);
static int comfort_resume_prompt(const char *id,int local);
static void comfort_scope(char *scope,int local) {
    if(local)strcpy(scope,"local");
    else snprintf(scope,80,"%s:%d:%d",server_host,server_port,server_https);
}
static int comfort_expired(void) {
    if(comfort_deadline && (unsigned long long)sceKernelGetSystemTimeWide()>=comfort_deadline) {
        comfort_deadline=0;comfort_timer_stopped=1;return 1;
    }
    return 0;
}
static void comfort_finished(const char *id,const char *name,int audio,int local,int result) {
    char scope[80];comfort_scope(scope,local);
    /* Failed startup must not replace an existing bookmark with zero. */
    if(result>=0 || (!audio && playback_position_ms>stream_start_seconds*1000)) {
        int index=comfort_find(&comfort_store,scope,id,1);
        if(index>=0) {
            ComfortRecord *r=&comfort_store.records[index];
            snprintf(r->name,sizeof(r->name),"%s",name&&*name?name:id);
            r->audio=audio;r->folder=0;r->used=++comfort_store.sequence;
            if(!audio)r->seconds=playback_reached_end?0:playback_position_ms/1000;
            if(!comfort_save_file(COMFORT_PATH,&comfort_store))snprintf(status,sizeof(status),"%s",tr(TXT_SETTINGS_FAILED));
        }
    }
    if(playback_reached_end && comfort_remaining>0 && --comfort_remaining==0)comfort_timer_stopped=1;
    if(comfort_timer_stopped)playback_reached_end=resume_pending=seek_requested=0;
}

static const char *audio_quality_name(void) {
    static const char *names[] = {"96k", "128k", "160k", "v6", "v5", "v4", "v3"};
    return names[selected_audio_quality];
}

static void load_playback_settings(void) {
    SceUID file = sceIoOpen(SETTINGS_PATH, PSP_O_RDONLY, 0);
    char data[1024], *line;
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
                if (!strncmp(host, "https://", 8)) {host+=8;server_https=1;}
                else if (!strncmp(host, "http://", 7)) {host+=7;server_https=0;}
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
            else if (!strncmp(line,"https=",6)) server_https=atoi(line+6)!=0;
            else if (!strncmp(line,"debug=",6)) debug_enabled=!strcmp(line+6,"1");
            else if (!strncmp(line, "server_password=", 16)) {
                strncpy(server_password,line+16,sizeof(server_password)-1);
                server_password[sizeof(server_password)-1]=0;
            }
            else if (!strncmp(line, "audio=", 6)) selected_audio_track = atoi(line + 6);
            else if (!strncmp(line, "subtitle=", 9)) selected_subtitle_track = atoi(line + 9);
            else if (!strncmp(line, "quality=", 8)) selected_audio_quality = atoi(line + 8);
            else if (!strncmp(line, "music_preset=", 13) && preset_path_valid(line+13)) strcpy(music_preset_file,line+13);
            else if (!strncmp(line,"preset_auto=",12)) music_preset_auto=atoi(line+12);
            else if (!strncmp(line,"preset_seconds=",15)) music_preset_seconds=atoi(line+15);
            else if (!strncmp(line,"preset_fade_ms=",15)) music_preset_fade_ms=atoi(line+15);
            else if (visual_option_parse(line)) {}
            else if (!strncmp(line,"milkdrop_high_resolution=",25)) md_high_resolution=atoi(line+25)!=0;
            else if (!strncmp(line, "video_fps=", 10)) selected_video_fps = !strcmp(line + 10, "24000/1001");
            else if (!strncmp(line, "play_mode=", 10)) download_before_play = !strcmp(line + 10, "download");
            else if (!strncmp(line, "volume=", 7)) playback_volume = atoi(line + 7);
            else if (!strncmp(line, "shuffle=", 8)) audio_shuffle = atoi(line + 8) != 0;
            else if (!strncmp(line, "language=", 9)) language_set_code(line + 9);
            else if (!strncmp(line, "tv_ui=", 6)) tv_ui_auto = !strcmp(line + 6, "auto");
            else if (!strncmp(line,"music_cpu_mhz=",14))music_cpu_mhz=atoi(line+14);
            else if (!strncmp(line,"milkdrop_cpu_mhz=",17))milkdrop_cpu_mhz=atoi(line+17);
            else if (!strncmp(line,"idle_cpu_mhz=",13))idle_cpu_mhz=atoi(line+13);
            else if (!strncmp(line,"video_cpu_mhz=",14))video_cpu_mhz=atoi(line+14);
            else if (!strncmp(line,"screen_idle=",12))screen_idle=atoi(line+12);
        }
    }
    if (selected_audio_track < 0 || selected_audio_track > 7) selected_audio_track = 0;
    if (selected_subtitle_track < -1 || selected_subtitle_track > 31) selected_subtitle_track = -1;
    if (selected_audio_quality < 0 || selected_audio_quality > 6) selected_audio_quality = 2;
    audio_shuffle = audio_shuffle != 0;
    if (playback_volume < 0 || playback_volume > 30) playback_volume = 24;
    if (server_port < 1 || server_port > 65535) server_port = PSP_STREAMER_PORT;
    if (!server_host[0]) strcpy(server_host, PSP_STREAMER_HOST);
    server_auth_update();
    if(!playback_clock_valid(music_cpu_mhz))music_cpu_mhz=0;
    if(!playback_clock_valid(milkdrop_cpu_mhz))milkdrop_cpu_mhz=0;
    if(!playback_clock_valid(idle_cpu_mhz))idle_cpu_mhz=0;
    if(!playback_clock_valid(video_cpu_mhz))video_cpu_mhz=0;
    if(screen_idle<0||screen_idle>2)screen_idle=0;
    if(music_preset_auto<0 || music_preset_auto>3) music_preset_auto=0;
    if(music_preset_seconds<30 || music_preset_seconds>600) music_preset_seconds=60;
    if(music_preset_fade_ms<0 || music_preset_fade_ms>5000) music_preset_fade_ms=1500;
}

static int save_playback_settings(void) {
    SceUID file;
    char data[2048];
    int length = snprintf(data, sizeof(data), "server=%s\nport=%d\nserver_password=%s\naudio=%d\nsubtitle=%d\nquality=%d\nvolume=%d\nshuffle=%d\nlanguage=%s\ntv_ui=%s\n",
                          server_host, server_port, server_password, selected_audio_track, selected_subtitle_track, selected_audio_quality, playback_volume, audio_shuffle, language_code(), tv_ui_auto ? "auto" : "off");
    length += snprintf(data + length, sizeof(data) - length, "video_fps=%s\n", selected_video_fps ? "24000/1001" : "20");
    length += snprintf(data + length, sizeof(data) - length, "play_mode=%s\n",download_before_play?"download":"stream");
    length += snprintf(data + length, sizeof(data) - length, "music_preset=%s\n",music_preset_file);
    length += snprintf(data+length,sizeof(data)-length,"preset_auto=%d\npreset_seconds=%d\npreset_fade_ms=%d\n",music_preset_auto,music_preset_seconds,music_preset_fade_ms);
    length += snprintf(data+length,sizeof(data)-length,"milkdrop_high_resolution=%d\n",md_high_resolution);
    length += snprintf(data+length,sizeof(data)-length,"https=%d\n",server_https);
    length += snprintf(data+length,sizeof(data)-length,"debug=%d\n",debug_enabled);
    length += snprintf(data+length,sizeof(data)-length,"music_cpu_mhz=%d\nvideo_cpu_mhz=%d\nscreen_idle=%d\n",music_cpu_mhz,video_cpu_mhz,screen_idle);
    length += snprintf(data+length,sizeof(data)-length,"milkdrop_cpu_mhz=%d\nidle_cpu_mhz=%d\n",milkdrop_cpu_mhz,idle_cpu_mhz);
    for(int i=0;i<VISUAL_OPTION_COUNT;i++) {
        if(length<0 || length>=(int)sizeof(data))return -1;
        length+=snprintf(data+length,sizeof(data)-length,"%s=%d\n",visual_options[i].key,*visual_options[i].value);
    }
    if(length<0 || length>=(int)sizeof(data))return -1;
    file = sceIoOpen(SETTINGS_PATH ".tmp", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0600);
    if(file<0)return file;
    int written=sceIoWrite(file,data,length), closed=sceIoClose(file);
    memset(data,0,sizeof(data));
    if(written!=length || closed<0)return -1;
    SceIoStat info;
    int existed=sceIoGetstat(SETTINGS_PATH,&info)>=0;
    sceIoRemove(SETTINGS_PATH ".bak");
    if(existed && sceIoRename(SETTINGS_PATH,SETTINGS_PATH ".bak")<0)return -1;
    if(sceIoRename(SETTINGS_PATH ".tmp",SETTINGS_PATH)<0) {
        if(existed)sceIoRename(SETTINGS_PATH ".bak",SETTINGS_PATH);
        return -1;
    }
    return 0;
}

static int resolve_server_address(struct in_addr *address) {
    if (inet_aton(server_host, address)) {
        cached_server_address = *address;
        have_cached_server_address = 1;
        return 0;
    }
    /* Firmware DNS can hang after a link transition despite its timeout.
     * Browsing already resolved this host: reuse that address for playback
     * and recovery. Settings/server changes explicitly invalidate the cache. */
    if(have_cached_server_address) {*address=cached_server_address;return 0;}
    unsigned char workspace[1024];
    int resolver=-1,result=sceNetResolverCreate(&resolver,workspace,sizeof(workspace));
    if(result<0) {
        sceNetResolverInit();
        result=sceNetResolverCreate(&resolver,workspace,sizeof(workspace));
    }
    if(result>=0) {
        result=sceNetResolverStartNtoA(resolver,server_host,address,2,1);
        sceNetResolverDelete(resolver);
    }
    if(result<0) {
        if (have_cached_server_address) { *address = cached_server_address; return 0; }
        return -1;
    }
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

#include "server_connection.h"
#include "diagnostic_history.h"
#include "recovery_log.h"
#include "playback_transport.h"

/* Used only after HTTP headers arrived.  A timeout is not an error: it lets
 * the playback owner stop an audio worker after WLAN disappears. */
static int stream_recv(int socket_fd, void *buffer, int length, int timeout_ms) {
    if(server_https)return tls_recv(socket_fd,buffer,length,timeout_ms);
    struct SceNetInetPollfd pollfd = { socket_fd, SCE_NET_INET_POLLIN, 0 };
    int ready = sceNetInetPoll(&pollfd, 1, timeout_ms);
    if (ready == 0) return -2;
    if (ready < 0) return 0;
    /* EOF may arrive together with the last readable bytes. Drain them. */
    if (!(pollfd.revents & SCE_NET_INET_POLLIN)) return 0;
    return (int)connection_recv(socket_fd, buffer, length, 0);
}

/* Never let a lost hotspot leave the UI or playback thread in a permanent
 * blocking recv().  Callers treat a timeout exactly like a dropped stream. */

static void keep_awake(void) {
    static unsigned long long last_tick;
    unsigned long long now = sceKernelGetSystemTimeWide();
    if (now - last_tick >= 30000000ULL) {
        int allow_display_idle=power_allow_display_idle(display_output.tv);
        scePowerTick(allow_display_idle?PSP_POWER_TICK_SUSPEND:PSP_POWER_TICK_ALL);
        last_tick = now;
    }
}

int exit_callback(int arg1, int arg2, void *common) {
    (void)arg1; (void)arg2; (void)common;
    prepare_oc_exit();
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

static int radio_connect_wait;
static int wifi_wait_tick(void) {
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(&pad,1);
    if(pad.Buttons & PSP_CTRL_CIRCLE || (radio_connect_wait && (pad.Buttons & PSP_CTRL_START)))return 1;
    keep_awake();
    sceKernelDelayThreadCB(100000);
    return 0;
}
#include "wifi_connection.h"
static int wait_for_network(int connect_wifi) {
    int result=wifi_initialize();
    if(result<0)return result;
    return connect_wifi?wifi_associate(0):-1;
}

static int wait_for_network_restore(void) {
    int result=wifi_associate(0);
    network_ready=http_ready=(result==0);
    return result;
}

static int http_get_wait(const char *path, char *buffer, int buffer_size, int idle_timeout_ms) {
    struct sockaddr_in server;
    char request[2048], *body;
    int socket_fd, received = 0, read_size, content_length = -1, header_length = -1, idle_ms = 0;
    socket_fd = sceNetInetSocket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) return socket_fd;
    if (prepare_server(&server) < 0) { connection_close(socket_fd); return -1004; }
    if (connection_connect(socket_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        int error = sceNetInetGetErrno();
        connection_close(socket_fd);
        return error ? -error : -1001;
    }
    snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n%s\r\n", path, server_host, server_auth_header);
    if ((int)connection_send(socket_fd, request, strlen(request), 0) < 0) { connection_close(socket_fd); return -1002; }
    while (received < buffer_size - 1) {
        read_size = stream_recv(socket_fd, buffer + received, buffer_size - 1 - received, 250);
        if (read_size == -2) {
            idle_ms += 250;
            if (idle_ms < idle_timeout_ms) continue;
            connection_close(socket_fd);
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
    connection_close(socket_fd);
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

#include "remote_http.h"
#include "remote_input_impl.h"
#include "menu_artwork.h"
#include "media_request.h"

static int http_get_binary(const char *path, unsigned char *buffer, int buffer_size) {
    struct sockaddr_in server;
    char request[2048], header[4096], *body = NULL;
    int socket_fd, received = 0, header_size = 0, body_size, content_length = -1, idle_ms = 0;
    socket_fd = sceNetInetSocket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) return -1;
    if (prepare_server(&server) < 0) {connection_close(socket_fd);return -1;}
    if (connection_connect(socket_fd, (struct sockaddr *)&server, sizeof(server)) < 0) { connection_close(socket_fd); return -1; }
    snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n%s\r\n", path, server_host, server_auth_header);
    if ((int)connection_send(socket_fd, request, strlen(request), 0) < 0) { connection_close(socket_fd); return -1; }
    while (header_size < (int)sizeof(header) - 1) {
        int got = stream_recv(socket_fd, header + header_size, sizeof(header) - 1 - header_size, 250);
        if (got == -2) { if ((idle_ms += 250) < 20000) continue; connection_close(socket_fd); return -1; }
        if (got <= 0) { connection_close(socket_fd); return -1; }
        idle_ms = 0;
        header_size += got; header[header_size] = 0; body = strstr(header, "\r\n\r\n"); if (body) break;
    }
    if (!body || !strstr(header, " 200 ")) { connection_close(socket_fd); return -1; }
    { char *length_header = strstr(header, "Content-Length:"); if (length_header) content_length = atoi(length_header + 15); }
    body += 4; body_size = header_size - (int)(body - header);
    if (body_size > buffer_size) { connection_close(socket_fd); return -1; }
    memcpy(buffer, body, body_size); received = body_size;
    while (received < buffer_size && (content_length < 0 || received < content_length)) {
        int wanted = buffer_size - received;
        int got;
        if (content_length >= 0 && wanted > content_length - received) wanted = content_length - received;
        got = stream_recv(socket_fd, buffer + received, wanted, 250);
        if (got == -2) { if ((idle_ms += 250) < 20000) continue; connection_close(socket_fd); return -1; }
        if (got <= 0) break;
        idle_ms = 0; received += got;
    }
    connection_close(socket_fd); return received;
}

/* The subtitle endpoint deliberately emits a restricted JSON form:
 * {"t":"text","c":[[start,end,"ASCII text"],...]}.  Text has already
 * been normalised by the server, so a narrow parser is both safer and much
 * smaller than adding a general JSON library to the playback binary. */
#include "offline_source.h"

static int prepare_client_subtitles(const char *media_id, int tv_profile) {
    char path[ID_SIZE + 128];
    int result;
    subtitle_cue_count = 0;
    subtitle_client_side = 0;
    subtitle_pages_live=subtitle_page_ready=subtitle_paged_response=subtitle_page_failures=0;
    if(subtitle_pages) {free(subtitle_pages);subtitle_pages=NULL;subtitle_cues=NULL;}
    if (subtitle_cues) { free(subtitle_cues); subtitle_cues = NULL; }
    if (selected_subtitle_track < 0 && !offline_active) return 0;
    snprintf(path, sizeof(path), "/api/subtitles/%s?track=%d&tv=%d&timebase=ms&page=1&at_ms=%d", media_id, selected_subtitle_track, tv_profile, stream_start_seconds*1000);
    result = offline_active ? offline_subtitle_json() : media_request_get(path, response, sizeof(response), 210000, 1);
    /* Offline overlays are optional (no subs, or already burned into video).
     * Preserve that behaviour; only a failed online request blocks startup. */
    if (result < 0) return offline_active ? 0 : result;
    /* Bitmap tracks use PSP sprites on LCD and server burn-in on TV. */
    if (strstr(response, "\"t\":\"bitmap\"") || (offline_active && strstr(response, "\"t\":\"pgs\""))) {
        char path[ID_SIZE + 64], *cursor;
        /* TV playback burns bitmap subtitles into the stream. Do not first
         * download/extract the complete track only to discard its cues. */
        if(tv_profile && !offline_active)return 0;
        snprintf(path, sizeof(path), "/api/bitmap-subtitles/%s?track=%d&tv=%d&timebase=ms", media_id, selected_subtitle_track, tv_profile);
        if(!offline_active) {
            result=media_request_get(path,response,sizeof(response),630000,1);
            if(result<0)return result;
        }
        if(!strstr(response, "\"t\":\"pgs\""))return 0;
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
    subtitle_paged_response=!offline_active && strstr(response,"\"paged\":1")!=NULL;
    subtitle_client_side = 1;
    return 0;
}

/* Do not reserve the cue table in the EBOOT's permanent BSS.  sceMpegInit
 * requires a substantial contiguous allocation on 6.61; allocating text
 * data only after AVC is live preserves the previously proven start budget. */
static void subtitle_parse_prepared_response(void) {
    char *cursor;
    if (!subtitle_client_side || subtitle_cues) return;
    if(subtitle_paged_response) {
        subtitle_pages=memalign(64,2*sizeof(*subtitle_pages));
        if(!subtitle_pages || !subtitle_page_parse(response,subtitle_pages)) {
            free(subtitle_pages);subtitle_pages=NULL;subtitle_client_side=0;return;
        }
        subtitle_page_bank=0;subtitle_cues=subtitle_pages[0].cues;
        subtitle_cue_count=subtitle_pages[0].count;
        subtitle_page_position=stream_start_seconds*1000;
        __sync_synchronize();subtitle_pages_live=1;
        return;
    }
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
        if (sscanf(entry, "[%d,%d,\"%159[^\"]\"]%n", &cue->start_ms,
                   &cue->end_ms, cue->text, &consumed) != 3 || consumed <= 0) break;
        if (cue->end_ms > cue->start_ms) subtitle_cue_count++;
        cursor = entry + consumed;
    }
}

static void subtitle_release(void) {
    /* The remote worker is joined before this owner releases either bank. */
    subtitle_pages_live=0;
    if(subtitle_pages)free(subtitle_pages);
    else if (subtitle_cues) free(subtitle_cues);
    subtitle_pages=NULL;subtitle_page_ready=0;
    if (subtitle_font) free(subtitle_font);
    subtitle_cues = NULL;
    subtitle_font = NULL;
    subtitle_cue_count = 0;
    if (bitmap_cues) free(bitmap_cues);
    bitmap_cues = NULL; bitmap_cue_count = 0; bitmap_client_side = 0; bitmap_loaded_cue = -1;
}

/* Sole render-thread consumer. Publish the freed bank only after switching;
 * background HTTP/JSON parsing never touches the bank being drawn. */
static void subtitle_page_advance(int position_ms) {
    subtitle_page_position=position_ms;
    if(!subtitle_pages_live || !subtitle_page_ready)return;
    __sync_synchronize();
    if(position_ms<subtitle_pages[subtitle_page_bank].until)return;
    subtitle_page_bank=1-subtitle_page_bank;
    subtitle_cues=subtitle_pages[subtitle_page_bank].cues;
    subtitle_cue_count=subtitle_pages[subtitle_page_bank].count;
    __sync_synchronize();subtitle_page_ready=0;
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
        got = offline_active ? offline_bitmap(index,(unsigned char *)response,RESPONSE_SIZE) : http_get_binary(path, (unsigned char *)response, RESPONSE_SIZE);
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
        destination = &playback_draw_target[y * VIDEO_STRIDE + x];
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

static void subtitle_draw_glyph(u32 *vram, int glyph, int left, int top,
                                int stride, int width, int height) {
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
                    if ((dx || dy) && px + dx >= 0 && px + dx < width && py + dy >= 0 && py + dy < height)
                        vram[(py + dy) * stride + px + dx] = 0x00000000;
            } else {
                if (px >= 0 && px < width && py >= 0 && py < height)
                    vram[py * stride + px] = 0x00ffffff;
            }
        }
    }
}

static const char *subtitle_draw_line(const char *text, int y, u32 *vram,
                                      int stride, int width, int height) {
    const char *cursor = text, *end = text, *last_space = NULL;
    int count = 0, glyph, index, left;
    while (*end && *end != '|' && count < (width / 11 - 1)) {
        const char *before = end;
        glyph = subtitle_utf8_char(&end);
        if (glyph == ' ') last_space = before;
        count++;
    }
    if (*end && *end != '|' && last_space) end = last_space;
    left = (width - count * 11) / 2;
    if (left < 4) left = 4;
    for (index = 0; cursor < end; index++) {
        glyph = subtitle_utf8_char(&cursor);
        subtitle_draw_glyph(vram, glyph, left + index * 11, y, stride, width, height);
    }
    while (*end == ' ') end++;
    if (*end == '|') end++;
    return end;
}

static void subtitle_present(int position_ms) {
    int index = -1, cue_index, y;
    u32 *vram;
    if (!subtitle_client_side || !subtitle_cues) return;
    for (cue_index = 0; cue_index < subtitle_cue_count; cue_index++) {
        if (position_ms >= subtitle_cues[cue_index].start_ms &&
            position_ms < subtitle_cues[cue_index].end_ms) { index = cue_index; break; }
        if (subtitle_cues[cue_index].start_ms > position_ms) break;
    }
    if (index < 0) return;
    vram = playback_draw_target;
    if (!subtitle_font) subtitle_load_font();
    if (!subtitle_font) return; /* Never risk AVC for an optional font asset. */
    {
        const char *text = subtitle_cues[index].text;
        for (y = 0; *text && y < 3; y++) {
            const char *next = subtitle_draw_line(text, (video_fullscreen || !receiver_visible ? 210 : 12) + y * 20,
                                                  vram, VIDEO_STRIDE, VIDEO_WIDTH, VIDEO_HEIGHT);
            if (next == text) break;
            text = next;
        }
    }
}

static void tvout_subtitle_present(int position_ms) {
    int index = -1, cue_index, y;
    u32 *vram = playback_draw_target;
    if (!subtitle_client_side || !subtitle_cues) return;
    for (cue_index = 0; cue_index < subtitle_cue_count; cue_index++) {
        if (position_ms >= subtitle_cues[cue_index].start_ms &&
            position_ms < subtitle_cues[cue_index].end_ms) { index = cue_index; break; }
        if (subtitle_cues[cue_index].start_ms > position_ms) break;
    }
    if (index < 0) return;
    if (!subtitle_font) subtitle_load_font();
    if (!subtitle_font) return;
    {
        const char *text = subtitle_cues[index].text;
        for (y = 0; *text && y < 3; y++) {
            const char *next = subtitle_draw_line(text, 408 + y * 20, vram,
                                                  TVOUT_STRIDE, 720, 480);
            if (next == text) break;
            text = next;
        }
    }
}

/* A deliberately tiny playback HUD: it costs a few VRAM writes, not a second
 * framebuffer or font renderer.  It makes pause state and progress visible
 * while retaining the proven direct Media-Engine presentation path. */
static void playback_hud(int frames, int paused) {
    u32 *vram = playback_draw_target;
    int x, y = VIDEO_HEIGHT - 5, filled = 0;
    (void)frames;
    if (current_duration_seconds > 0.0f)
        filled = (int)(VIDEO_WIDTH * (playback_position_ms / 1000.0) / current_duration_seconds);
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

/* The native TV frame has a 768-pixel pitch and cannot share the LCD HUD's
 * 512-pixel drawing helpers.  Keep this first overlay deliberately tiny: it
 * is redrawn after CSC on every frame and therefore has no extra buffer or
 * timing cost. */
static void tvout_playback_hud(int frames, int paused) {
    u32 *vram = playback_draw_target;
    int x, y, filled = 0;
    (void)frames;
    if (current_duration_seconds > 0.0f)
filled = (int)(720 * (playback_position_ms / 1000.0) / current_duration_seconds);
    if (filled < 0) filled = 0;
    if (filled > 720) filled = 720;
    for (x = 0; x < 720; x++) {
        vram[474 * TVOUT_STRIDE + x] = x < filled ? 0x00D8E8FF : 0x00202020;
        vram[475 * TVOUT_STRIDE + x] = x < filled ? 0x00D8E8FF : 0x00202020;
    }
    if (paused) {
        for (y = 20; y < 62; y++) for (x = 660; x < 710; x++) {
            if ((x >= 670 && x < 682) || (x >= 690 && x < 702))
                vram[y * TVOUT_STRIDE + x] = 0x00FFFFFF;
            else if (x == 660 || x == 709 || y == 20 || y == 61)
                vram[y * TVOUT_STRIDE + x] = 0x00000000;
        }
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
    int right=VIDEO_WIDTH,bottom=VIDEO_HEIGHT;
    int bounded=theme_text_active && theme_text_edges(0,left,top,&right,&bottom);
    u32 *vram = (u32 *)0x44000000;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    if (!subtitle_font) subtitle_load_font();
    if (!subtitle_font) return;
    if(top+8>bottom)return;
    int columns=(right-left+1)/7;
    if(columns<=0)return;
    const char *measure=line;int count=0;
    while(*measure){subtitle_utf8_char(&measure);count++;}
    cursor = line;
    while (*cursor && index<columns) {
        glyph = subtitle_utf8_char(&cursor);
        if(bounded && count>columns && columns>=3 && index>=columns-3)glyph='.';
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
    u32 *vram = playback_draw_target;
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

/* Load firmware AV dependencies once through the Utility API. Keep the
 * proven MPEG module order; direct flash-PRX loading is not a user-mode API. */
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


#include "timed_stream.h"

#include "audio_lease.h"
#include "sync_trace.h"
#include "video_watchdog.h"

/* Serialize all ME codec and cache transactions, including initialization.
 * Neither networking, DAC output nor display waits may hold this semaphore. */
static SceUID codec_sema = -1;
static int codec_enter(void) {
    if (!timed_active) return 1;
    while (timed_running) {
        SceUInt timeout = 10000;
        if (sceKernelWaitSema(codec_sema, 1, &timeout) >= 0) return 1;
    }
    return 0;
}
static void codec_leave(void) {
    if (timed_active) sceKernelSignalSema(codec_sema, 1);
}
static unsigned char *video_staging;
static int video_staging_bytes;

/* Prepare exactly once into RAM; nothing here writes the visible scanout.
 * The owner can hold or discard this finished picture after reading fresh
 * audio PTS, like PPA's show thread, while continuing to process controls. */
static int prepare_timed_video(TimedPacket *packet) {
    int result, cue_ms, saved_position = playback_position_ms;
    unsigned long long start = debug_enabled ? sceKernelGetSystemTimeWide() : 0;
    video_watch_ping("ME lock for video");
    if (!codec_enter()) return -1324;
    video_watch_ping("AVC decode/CSC");
    result = h264_hw_decode_avcc((const AvcPacket *)packet->data, packet->size, video_staging);
    codec_leave();
    if(debug_enabled) sync_decode_us = (unsigned int)(sceKernelGetSystemTimeWide() - start);
    if (result < 0) {
        video_step = h264_hw_last_step();
        if(debug_enabled) {
            const AvcPacket *avc=(const AvcPacket *)packet->data;
            char diagnostic[384];
            snprintf(diagnostic,sizeof(diagnostic),
                "decoder failure: code=%08X stage=%s pts=%d bytes=%d position_ms=%d "
                "packet_valid=%d free=%u largest=%u video_queue=%u audio_queue=%u\n",
                (unsigned int)result,video_step,packet->pts,packet->size,playback_position_ms,
                avc && avcc_packet_valid(avc,packet->size),
                (unsigned int)sceKernelTotalFreeMemSize(),(unsigned int)sceKernelMaxFreeMemSize(),
                timed_video.write-timed_video.read,timed_audio.write-timed_audio.read);
            video_watch_write(diagnostic,0);
        }
        return result;
    }
    if (result > 0) {
        video_watch_ping("subtitle/overlay");
        cue_ms = offline_active ? packet->pts : stream_start_seconds * 1000 + packet->pts - timed_video_origin;
        playback_position_ms = cue_ms;
        subtitle_page_advance(cue_ms);
        playback_draw_target = (u32 *)video_staging;
        if (!tvout_video_active) {
            subtitle_present(cue_ms);
            bitmap_present(cue_ms, audio_media_id);
            if (video_fullscreen || !receiver_visible) playback_hud(0, playback_paused);
            else receiver_hud(0);
        } else {
            tvout_subtitle_present(cue_ms);
            tvout_playback_hud(0, playback_paused);
        }
        playback_draw_target = (u32 *)0x44000000;
        playback_position_ms = saved_position;
        hardware_decoder_frames++;
    }
    if(debug_enabled) sync_prepare_us = (unsigned int)(sceKernelGetSystemTimeWide() - start);
    return result;
}


#include "milkdrop_wave.h"
static void audio_measure_pcm(const short *pcm, int frames) {
    visualization_pcm_publish(pcm, frames);
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
    AudioLease lease = {-1};
    unsigned int previous_pts = 0;
    int have_previous_pts = 0;
    (void)args; (void)argp;
    sceAudioSRCChRelease();
    channel = sceAudioChReserve(0, dac_samples, PSP_AUDIO_FORMAT_STEREO);
    if (channel < 0) { audio_state = -16; audio_running = 0; return 0; }
    sync_audio_channel = channel;
    while (1) {
        while (audio_running && (!audio_start || !audio_queue_primed ||
                                 (timed_active && !video_first_presented))) {
            sceKernelDelayThread(1000);
        }
        if (!audio_start || (timed_active && !timed_running)) break;
        if (!audio_queue_wait(audio_queue_ready_sema)) {
            /* Release the last slot at a real drain, including at EOF and
             * underrun. It must not wait for a nonexistent next packet. */
            if (lease.held >= 0 && sceAudioGetChannelRestLen(channel) == 0) {
                audio_lease_drain(&lease);
                sceKernelSignalSema(audio_queue_free_sema, 1);
                audio_played_blocks++;
            }
            if (!audio_running && lease.held < 0) break;
            continue;
        }
        block = audio_queue_read;
        audio_queue_read = (audio_queue_read + 1) % AUDIO_QUEUE_BLOCKS;
        /* Keep PPA's submitted-block clock, measured separately from the
         * hardware's remaining sample count in the diagnostic trace. */
        audio_current_timestamp_ms = audio_block_timestamp_ms[block];
        if(debug_enabled) {
            if (have_previous_pts && audio_current_timestamp_ms <= previous_pts) sync_audio_pts_errors++;
            previous_pts = audio_current_timestamp_ms; have_previous_pts = 1;
        }
        audio_clock_started = 1;
        sceKernelDcacheWritebackRange(audio_samples + block * AUDIO_BLOCK_SAMPLES * 2, block_bytes);
        if (sceAudioOutputBlocking(channel, PSP_AUDIO_VOLUME_MAX * playback_volume / 30,
                                   audio_samples + block * AUDIO_BLOCK_SAMPLES * 2) < 0) {
            audio_state = -20; audio_running = 0; break;
        }
        /* As in PPA: the first submission frees nothing; a later successful
         * submission releases the PREVIOUS slot, never the just-submitted one. */
        if (audio_lease_submit(&lease, block) >= 0) {
            sceKernelSignalSema(audio_queue_free_sema, 1);
            audio_played_blocks++;
        }
        if (audio_state >= 0) audio_state = 16;
    }
    /* No worker is allowed to recycle held PCM during channel teardown. */
    {
        unsigned long long stop = sceKernelGetSystemTimeWide();
        while (sceAudioGetChannelRestLen(channel) > 0 &&
               sceKernelGetSystemTimeWide() - stop < 500000ULL)
            sceKernelDelayThread(1000);
    }
    sceAudioChRelease(channel);
    sync_audio_channel = -1;
    if (audio_lease_drain(&lease) >= 0) {
        sceKernelSignalSema(audio_queue_free_sema, 1);
        audio_played_blocks++;
    }
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
    char request[2048], header[4096], *body = NULL;
    int socket_fd = -1, header_size = 0, received, output_thread_id = -1;
    SceUID local_fd = -1;
    TimedPacket timed_packet = {0};
    unsigned int block_pts = 0;
    int have = 0, frame_size, result, initial_size, frames_in_block = 0;
    int write_slot_reserved = 0;
    unsigned long long last_data=sceKernelGetSystemTimeWide();
    Mp3Preroll preroll={0};
    const int block_bytes = audio_dac_samples * 2 * (int)sizeof(short);
    const int decoded_bytes = MP3_DECODE_SAMPLES * 2 * (int)sizeof(short);
    (void)args; (void)argp;
    audio_state = 10;
    if (!codec_enter()) { audio_state = -27; goto cleanup; }
    memset(mp3_codec, 0, sizeof(mp3_codec));
    if (sceAudiocodecCheckNeedMem(mp3_codec, PSP_CODEC_MP3) < 0) {
        codec_leave(); audio_state = -21; goto cleanup;
    }
    /* The firmware codec's ME-side DMA touches whole cache lines; reserve a
     * rounded work area, not merely the nominal byte count it reports. */
    mp3_codec_work = memalign(64, (mp3_codec[4] + 63) & ~63UL);
    if (!mp3_codec_work) { codec_leave(); audio_state = -22; goto cleanup; }
    mp3_codec[3] = (unsigned long)mp3_codec_work;
    result = sceAudiocodecInit(mp3_codec, PSP_CODEC_MP3);
    codec_leave();
    if (result < 0) { audio_state = -23; goto cleanup; }
    if (!timed_active && offline_music) {
        local_fd=offline_open_movie(offline_movie,sizeof(offline_movie),offline_movie_size);
        if(local_fd<0){audio_state=local_fd;goto cleanup;}
    } else if (!timed_active) {
    /* Stand-alone music has no meaningful language/subtitle selection.  Its
     * first (and normally only) audio stream is always the source. */
    snprintf(request, sizeof(request), "GET /api/transcode/%s?container=mp3&profile=%s&audio=0&audio_quality=%s&start=%d HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n%s\r\n", audio_media_id, PSP_STREAMER_PROFILE, audio_quality_name(), stream_start_seconds, server_host, server_auth_header);
    socket_fd = sceNetInetSocket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) { audio_state = -11; goto cleanup; }
    audio_socket_fd = socket_fd;
    if (prepare_server(&server) < 0) { audio_state = -17; goto cleanup; }
    if (playback_connect(socket_fd, &server, &audio_running) < 0) { audio_state = -12; goto cleanup; }
    if (playback_send(socket_fd, request, strlen(request), &audio_running) < 0) { audio_state = -13; goto cleanup; }
    last_data=sceKernelGetSystemTimeWide();
    while (header_size < (int)sizeof(header) - 1) {
        if(!audio_running)goto cleanup;
        received=playback_recv(socket_fd,header+header_size,sizeof(header)-1-header_size,250);
        if(received==-2 && sceKernelGetSystemTimeWide()-last_data<180000000ULL)continue;
        if (received <= 0) { audio_state = -14; goto cleanup; }
        last_data=sceKernelGetSystemTimeWide();
        header_size += received; header[header_size] = '\0'; body = strstr(header, "\r\n\r\n");
        if (body) break;
    }
    if (!body || !strstr(header, " 200 ")) { audio_state = -15; goto cleanup; }
    initial_size = header_size - (int)(body + 4 - header);
    if (initial_size > MP3_INPUT_BUFFER_BYTES) initial_size = MP3_INPUT_BUFFER_BYTES;
    if (initial_size > 0) memcpy(mp3_input_buffer, body + 4, initial_size);
    have = initial_size;
    }
    audio_queue_read = audio_queue_write = 0;
    audio_queue_primed = audio_blocks_published = 0;
    if (audio_queue_create() < 0) { audio_state = -25; audio_running = 0; goto cleanup; }
    /* PMPlayer's output worker runs at ordinary playback priority.  The DAC
     * call itself blocks, so a very high priority only steals time from MP3
     * decoding and network refill around a block boundary. */
    output_thread_id = sceKernelCreateThread("PSPStreamerDAC", audio_output_thread, 0x3D, 0x2000, 0, NULL);
    if (output_thread_id < 0) { audio_state = -16; audio_running = 0; goto cleanup; }
    audio_output_thread_id = output_thread_id;
    if (sceKernelStartThread(output_thread_id, 0, NULL) < 0) {
        sceKernelDeleteThread(output_thread_id); audio_output_thread_id = -1;
        audio_state = -16; audio_running = 0; goto cleanup;
    }
    while (audio_running) {
        int warmup=0,missing_history=0;
        if (!write_slot_reserved) {
            if (!audio_queue_wait(audio_queue_free_sema)) continue;
            if (!audio_running) break;
            write_slot_reserved = 1;
        }
        if (timed_active) {
            timed_audio_waiting=1;
            while (timed_running && !timed_get(&timed_audio, &timed_packet)) {
                if (timed_eof) break;
            }
            timed_audio_waiting=0;
            if (!timed_packet.data) break;
            frame_size = mp3_frame_size(timed_packet.data, timed_packet.size);
            if (frame_size != timed_packet.size || frame_size > MP3_MAX_FRAME_BYTES) {
                audio_state = -26; break;
            }
            memcpy(mp3_input_buffer, timed_packet.data, frame_size);
            warmup=offline_active && timed_packet.pts<offline_seek_ms;
            if(warmup) {
                missing_history=mp3_preroll_missing(&preroll,mp3_input_buffer,frame_size);
                if(missing_history<0){audio_state=-26;break;}
            }
            if (!frames_in_block) block_pts = (unsigned int)timed_packet.pts;
            free(timed_packet.data); timed_packet.data = NULL;
            have = frame_size;
        } else {
        /* PCM backpressure and pause do not count as network inactivity. */
        last_data=sceKernelGetSystemTimeWide();
        while (have < 4 && audio_running) {
            received = offline_music ? sceIoRead(local_fd,mp3_input_buffer+have,MP3_INPUT_BUFFER_BYTES-have) :
                playback_recv(socket_fd, mp3_input_buffer + have, MP3_INPUT_BUFFER_BYTES - have, 250);
            if(offline_music && received<=0) {
                if(received<0)audio_state=received;
                else if(have)audio_state=-26;
                else offline_music_eof=1;
                goto cleanup;
            }
            if (received == -2) {
                if(!audio_start)last_data=sceKernelGetSystemTimeWide();
                if(sceKernelGetSystemTimeWide()-last_data<30000000ULL)continue;
                audio_state=-28;audio_running=0;break;
            }
            last_data=sceKernelGetSystemTimeWide();
            if (received <= 0) { if(received<0)audio_state=-28; audio_running = 0; break; }
            have += received;
        }
        if (!audio_running) break;
        frame_size = mp3_frame_size(mp3_input_buffer, have);
        if (frame_size < 0 || frame_size > MP3_MAX_FRAME_BYTES) { memmove(mp3_input_buffer, mp3_input_buffer + 1, --have); continue; }
        while (have < frame_size && audio_running) {
            received = offline_music ? sceIoRead(local_fd,mp3_input_buffer+have,MP3_INPUT_BUFFER_BYTES-have) :
                playback_recv(socket_fd, mp3_input_buffer + have, MP3_INPUT_BUFFER_BYTES - have, 250);
            if(offline_music && received<=0){audio_state=received<0?received:-26;goto cleanup;}
            if (received == -2) {
                if(!audio_start)last_data=sceKernelGetSystemTimeWide();
                if(sceKernelGetSystemTimeWide()-last_data<30000000ULL)continue;
                audio_state=-28;audio_running=0;break;
            }
            last_data=sceKernelGetSystemTimeWide();
            if (received <= 0) { if(received<0)audio_state=-28; audio_running = 0; break; }
            have += received;
        }
        if (!audio_running) break;
        }
        if (!codec_enter()) { audio_state = -27; break; }
        mp3_codec[6] = (unsigned long)mp3_input_buffer;
        mp3_codec[7] = mp3_codec[10] = frame_size;
        mp3_codec[8] = (unsigned long)(audio_samples + audio_queue_write * AUDIO_BLOCK_SAMPLES * 2 +
                                        frames_in_block * MP3_DECODE_SAMPLES * 2);
        mp3_codec[9] = decoded_bytes;
        sceKernelDcacheWritebackRange(mp3_input_buffer, frame_size);
        sceKernelDcacheWritebackInvalidateRange((void *)mp3_codec[8], decoded_bytes);
        result = sceAudiocodecDecode(mp3_codec, PSP_CODEC_MP3);
        /* Invalidate while holding the ME/cache lock: AVC's whole-cache
         * maintenance must not write stale PCM back over the DMA result. */
        sceKernelDcacheInvalidateRange((void *)mp3_codec[8], decoded_bytes);
        codec_leave();
        if (result < 0 && !(warmup && missing_history)) { audio_state = -24; audio_running = 0; break; }
        memmove(mp3_input_buffer, mp3_input_buffer + frame_size, have - frame_size);
        have -= frame_size;
        /* Never publish preroll PCM or its timestamps. Reuse the reserved
         * slot until the first audible frame at/after the video keyframe. */
        if(warmup)continue;
        frames_in_block++;
        if (frames_in_block * MP3_DECODE_SAMPLES == audio_dac_samples) {
            sceKernelDcacheInvalidateRange(audio_samples + audio_queue_write * AUDIO_BLOCK_SAMPLES * 2, block_bytes);
            sceKernelDcacheWritebackRange(audio_samples + audio_queue_write * AUDIO_BLOCK_SAMPLES * 2, block_bytes);
            audio_measure_pcm(audio_samples + audio_queue_write * AUDIO_BLOCK_SAMPLES * 2, audio_dac_samples);
            audio_block_timestamp_ms[audio_queue_write] = timed_active ? block_pts :
                (unsigned int)(((unsigned long long)audio_blocks_published *
                                (unsigned long long)audio_dac_samples * 1000ULL) / 44100ULL);
            audio_queue_write = (audio_queue_write + 1) % AUDIO_QUEUE_BLOCKS;
            sceKernelSignalSema(audio_queue_ready_sema, 1);
            frames_in_block = 0;
            write_slot_reserved = 0;
            audio_blocks_published++;
            if (audio_blocks_published >= audio_prefill_target) {
                audio_queue_primed = 1;
                if (audio_state >= 0) audio_state = 15;
            }
        }
    }
cleanup:
    free(timed_packet.data);
    if ((timed_active || (offline_music && offline_music_eof)) && frames_in_block && write_slot_reserved && audio_state >= 0 && (timed_active?timed_running:audio_running)) {
        int used = frames_in_block * MP3_DECODE_SAMPLES * 2;
        short *slot = audio_samples + audio_queue_write * AUDIO_BLOCK_SAMPLES * 2;
        sceKernelDcacheInvalidateRange(slot, block_bytes);
        memset(slot + used, 0, block_bytes - used * sizeof(short));
        audio_block_timestamp_ms[audio_queue_write] = timed_active ? block_pts :
            (unsigned int)((unsigned long long)audio_blocks_published*audio_dac_samples*1000ULL/44100ULL);
        sceKernelDcacheWritebackRange(slot, block_bytes);
        audio_queue_write = (audio_queue_write + 1) % AUDIO_QUEUE_BLOCKS;
        sceKernelSignalSema(audio_queue_ready_sema, 1);
        audio_blocks_published++;
        write_slot_reserved = 0;
    }
    if (timed_active) {
        audio_queue_primed = 1;
        while (timed_running && audio_state >= 0 && audio_played_blocks < audio_blocks_published)
            sceKernelDelayThread(10000);
        timed_audio_done = 1;
    }
    if(offline_music && offline_music_eof && audio_state>=0) {
        audio_queue_primed=1;audio_state=15;
        while(audio_running && audio_state>=0 && audio_played_blocks<audio_blocks_published)
            sceKernelDelayThread(10000);
    }
    if(local_fd>=0)sceIoClose(local_fd);
    /* A decoder failure or end-of-stream must wake the UI and DAC worker.
     * Previously this flag could remain true after the producer had gone,
     * leaving the player apparently frozen with an empty audio queue. */
    audio_running = 0;
    audio_start = 1;
    if (write_slot_reserved && audio_queue_free_sema >= 0)
        sceKernelSignalSema(audio_queue_free_sema, 1);
    if (audio_socket_fd == socket_fd) { audio_socket_fd = -1; if (socket_fd >= 0) connection_close(socket_fd); }
    if (mp3_codec_work) { free(mp3_codec_work); mp3_codec_work = NULL; }
    return 0;
}

#include "music_ui.h"
#include "tv_gui.h"
#include "lcd_music.h"
#include "spectrum_fullscreen.h"
#include "music_caption.h"
#include "video_controls.h"

static void draw_fullscreen_spectrum(void) {
    unsigned char bands[SPECTRUM_BANDS];
    int i,tv=tv_ui_active;
    unsigned long long now=sceKernelGetSystemTimeWide();
    if(tvout_video_active || display_output.tv!=tv) return;
    if(spectrum_fullscreen.valid && now<spectrum_fullscreen.next_tick) return;
    for(i=0;i<SPECTRUM_BANDS;i++) bands[i]=spectrum_levels[i];
    sceDisplayWaitVblankStart();
    spectrum_fullscreen_render((u32 *)0x44000000,tv?720:480,tv?480:272,
        tv?TV_GUI_STRIDE:VIDEO_STRIDE,bands,audio_running && audio_start);
    spectrum_fullscreen.next_tick=sceKernelGetSystemTimeWide()+MUSIC_UI_INTERVAL_US;
    if(tv) tv_music.next_tick=spectrum_fullscreen.next_tick;
    else lcd_music.next_tick=spectrum_fullscreen.next_tick;
}
static int json_value(const char *from, const char *key, char *destination, size_t length);
static int json_integer(const char *from, const char *key, int fallback);
#include "remote_state.h"
#define PSPSTREAMER_PLEX_REPORT 1
/* Published by the playback loop; reporting piggybacks on the existing HTTP
 * worker. Never contact Plex (or wait for it) from the DAC/display thread. */
static char plex_playing_id[ID_SIZE];
static char playback_report_id[ID_SIZE];
static volatile int plex_position_ms, plex_paused, plex_started;
static void plex_report_path(char *path, size_t capacity, int sequence, int stopped) {
    if (plex_playing_id[0] && plex_started)
        snprintf(path, capacity, "/api/remote/next?after=%d&plex=%s&state=%s&position=%d&duration=%d",
            sequence, plex_playing_id, stopped?"stopped":plex_paused?"paused":"playing",
            plex_position_ms, (int)(current_duration_seconds*1000.0f));
    else snprintf(path, capacity, "/api/remote/next?after=%d&state=%s&position=%d&duration=%d", sequence,
        stopped?"stopped":plex_paused?"paused":"playing",plex_position_ms,
        (int)(current_duration_seconds*1000.0f));
    if(playback_report_id[0] && !(plex_playing_id[0] && plex_started)) {
        size_t used=strlen(path);
        snprintf(path+used,capacity-used,"&media=%s",playback_report_id);
    }
    size_t used=strlen(path);
    snprintf(path+used,capacity-used,"&started=%d",plex_started);
}
static void plex_report_begin(const char *id) {
    snprintf(playback_report_id,sizeof(playback_report_id),"%s",id);
    snprintf(plex_playing_id,sizeof(plex_playing_id),"%s",
        (!strncmp(id,"plex.",5) || !strncmp(id,"jellyfin.",9))?id:"");
    plex_position_ms=stream_start_seconds*1000;plex_paused=plex_started=0;
}
static void plex_report_stop(int sequence) {
    if(playback_report_id[0]) {
        char path[ID_SIZE+192],reply[2048];volatile int running=1;
        plex_report_path(path,sizeof(path),sequence,1);
        remote_http_get_budget(path,reply,sizeof(reply),&running,1500);
    }
}
#include "music_remote.h"
#include "milkdrop_warp.h"
#include "milkdrop_preset.h"
#include "music_preset_ui.h"
#include "visual_options_ui.h"
#include "preset_browser.h"

/* Music-only session preferences, not GU/decoder state. Keep them across
 * autoplay, shuffle, remote replacement, seek and manual track selection.
 * Video uses its own presentation state; application restart resets these. */
static int music_saved_visual_preset;
static int music_saved_fullscreen;
static void music_transition_end(void) {
    if(!music_transition)return;
    md_stop();music_transition=0;music_visual_active=0;
}
static void music_transition_frame(void) {
    if(!music_transition)return;
    unsigned char silence[SPECTRUM_BANDS]={0};
    if(md_frame(tv_ui_active,music_saved_fullscreen,silence,0,sceKernelGetSystemTimeWide(),music_saved_visual_preset-1)<=0)
        music_transition_end();
}
/* 1: reconnect at live edge; 2: paused with all network/codec resources freed. */
static int radio_next_action;
static int music_network_failed;
static int radio_is_live(const char *id) { return !strncmp(id,"radio.",6); }

static int music_formula_trace_remaining;
static int music_formula_yields_remaining;
static void music_formula_trace(const char *event,int line,int detail) {
    if(!debug_enabled || music_formula_trace_remaining<=0)return;
    if(!strncmp(event,"VM yield",8)) {
        if(music_formula_yields_remaining<=0)return;
        music_formula_yields_remaining--;
    }
    music_formula_trace_remaining--;
    char text[192];
    snprintf(text,sizeof(text),"formula=%s source_line=%d detail=%d stack_free=%d\n",
        event,line,detail,sceKernelGetThreadStackFreeSize(0));
    video_watch_write(text,0);
}
static void music_visual_trace(const char *stage,int persist) {
    if(!strcmp(stage,"MilkDrop frame/shape formulas")) {
        music_formula_trace_remaining=persist?64:0;
        music_formula_yields_remaining=persist?8:0;
        pm_diagnostic_hook=debug_enabled && persist?music_formula_trace:NULL;
    }
    video_watch_ping(stage);
    if(debug_enabled && persist) {
        char text[640];
        snprintf(text,sizeof(text),"preset=%.511s stage=%s stack_free=%d\n",
            music_preset_file,stage,sceKernelGetThreadStackFreeSize(0));
        video_watch_write(text,0);
    }
}
static int play_audio_once(const char *media_id, const char *title) {
    music_network_failed=0;
    plex_report_begin(media_id);
    md_profile_reset(debug_enabled);
    md_trace_hook=debug_enabled?music_visual_trace:NULL;
    offline_music_eof=0;
    music_remote_action=MUSIC_REMOTE_NONE;
    int live=radio_is_live(media_id), last_radio_blocks=0;
    char radio_station[192],radio_song[192]="";
    snprintf(radio_station,sizeof(radio_station),"%s",title);
    snprintf(music_radio_id,sizeof(music_radio_id),"%s",live?media_id:"");
    music_radio_ready=0;
    unsigned long long radio_progress=sceKernelGetSystemTimeWide();
    radio_next_action=0;
    if(live)stream_start_seconds=0;
    video_file_direction=0;
    int audio_thread_id, paused = 0, fullscreen = music_saved_fullscreen, stopped_by_user = 0;
    int previous_ui_priority = -1;
    int remote_result = 0, start_result;
    spectrum_fullscreen_reset();
    video_step = "Music startup";
    start_result = video_watch_start(1);
    if (start_result < 0) { video_step = "Diagnostic file"; video_watch_stop(); return start_result; }
    /* Public modes: spectrum (0), file MilkDrop (4), cave (6). The GU
     * adapter's historical 0..2 test variants are no longer selectable. */
    int visual_preset = music_saved_visual_preset == 6 ? 6 : music_saved_visual_preset == 4 ? 4 : 0;
    MdFileError preset_error;
    int preset_result=MD_FILE_OK;
    int retained_visual=music_transition;
    music_transition=0;
    unsigned long long preset_notice_tick = ~0ULL;
    unsigned int old = 0;
    unsigned long long next_volume_repeat_tick = 0;
    PresetSequence *sequence=music_preset_auto?malloc(sizeof(*sequence)):NULL;
    if(sequence) preset_sequence_load_selected(sequence,"presets",music_preset_file,(unsigned int)sceKernelGetSystemTimeWide());
    unsigned long long next_preset_tick=sceKernelGetSystemTimeWide()+preset_interval_us();
    PresetCuts preset_cuts={0};
    md_preset_duration=(float)music_preset_seconds;
    strncpy(audio_media_id, media_id, sizeof(audio_media_id) - 1);
    audio_media_id[sizeof(audio_media_id) - 1] = '\0';
    audio_queue_read = audio_queue_write = audio_played_blocks = 0;
    audio_current_timestamp_ms = 0;
    memset(audio_block_timestamp_ms, 0, sizeof(audio_block_timestamp_ms));
    audio_queue_primed = audio_blocks_published = 0;
    vu_left = vu_right = vu_display_left = vu_display_right = 0;
    memset((void *)spectrum_levels, 0, sizeof(spectrum_levels));
    memset(spectrum_display, 0, sizeof(spectrum_display));
    audio_output_thread_id = -1;
    audio_prefill_target = AUDIO_MUSIC_PREFILL_BLOCKS;
    audio_dac_samples = AUDIO_BLOCK_SAMPLES;
    playback_reached_end = 0;
    timed_active = 0;
    if(!retained_visual)music_visual_active = 0;
    resume_pending = seek_requested = 0;
    video_first_presented = 1;
    /* Read once, before either music worker exists. Never parse on a draw tick. */
    if(!retained_visual) { char path[272]; snprintf(path,sizeof(path),"presets/%s",music_preset_file);
      if(access(path,F_OK)<0){strcpy(music_preset_file,"active.milk");snprintf(path,sizeof(path),"presets/active.milk");}
      preset_result = md_load_preset(path, &md_custom_preset, &preset_error); }
    audio_running = 1; audio_start = 1; audio_clock_started = 0; audio_state = 0;
    /* Neither music GUI may outrank the existing 0x3D DAC worker. Restore
     * the caller's priority before returning to menus or subsequent video. */
    previous_ui_priority = music_ui_lower_priority();
    /* The only initial full-frame draw happens BEFORE audio starts. */
    if (!retained_visual && tv_ui_active) {
        tv_music_reset();
        tv_draw_music(title, 0);
    } else if(!retained_visual) {
        lcd_music_reset();
        lcd_draw_music(title, 0);
    }
    /* Recreate resources before audio workers start. A missing/broken custom
     * file retains slot 4's normal diagnostic, never an active invalid effect.
     * Allocation failure falls back locally without erasing the preference. */
    if (visual_preset && (visual_preset != 4 || preset_result == MD_FILE_OK)) {
        if (md_start()) music_visual_active = 1;
        else visual_preset = 0;
    }
    if(music_visual_active) {
        subtitle_load_font();
        md_title(subtitle_font,current_media_artist,current_media_title[0]?current_media_title:title,sceKernelGetSystemTimeWide(),1);
    }
    if (fullscreen && !music_visual_active) draw_fullscreen_spectrum();
    /* A held Select that resumed live radio must not immediately pause again. */
    { SceCtrlData initial; sceCtrlPeekBufferPositive(&initial,1); old=initial.Buttons; }
    audio_thread_id = sceKernelCreateThread("PSPStreamerMusic", audio_thread, 0x18, server_https?0x10000:0x4000, 0, NULL);
    start_result = audio_thread_id < 0 ? audio_thread_id : sceKernelStartThread(audio_thread_id, 0, NULL);
    if (start_result < 0) {
        video_step = "Music worker";
        if (audio_thread_id >= 0) sceKernelDeleteThread(audio_thread_id);
        audio_running = 0;
        md_stop(); music_visual_active = 0;
        music_ui_restore_priority(previous_ui_priority);
        video_watch_stop();
        free(sequence);
        return start_result;
    }
    remote_result = offline_music ? 0 : music_remote_start();
    while (audio_running && remote_result >= 0) {
        playback_clock(music_visual_active?milkdrop_cpu_mhz:music_cpu_mhz);
        plex_paused=paused;
        if(playback_report_id[0]) {
            plex_position_ms=stream_start_seconds*1000+(int)((unsigned long long)audio_played_blocks*audio_dac_samples*1000/PSP_AUDIO_SAMPLE_RATE);
            plex_started=audio_played_blocks>0;
        }
        video_watch_ping("music loop");
        if(comfort_expired()){stopped_by_user=1;radio_next_action=0;break;}
        SceCtrlData pad;
        int action = music_remote_action;
        int seek_seconds = music_remote_seconds;
        if(live && action==MUSIC_REMOTE_SEEK) {music_remote_action=0;action=0;}
        if(live && action==MUSIC_REMOTE_PAUSE) {
            radio_next_action=2;stopped_by_user=1;break;
        }
        if (action == MUSIC_REMOTE_PAUSE || action == MUSIC_REMOTE_RESUME) {
            paused = action == MUSIC_REMOTE_PAUSE;
            audio_start = !paused;
        } else if (action == MUSIC_REMOTE_STOP || action == MUSIC_REMOTE_PLAY ||
                   action == MUSIC_REMOTE_SEEK) {
            stopped_by_user = 1;
            if (action == MUSIC_REMOTE_SEEK) {
                stream_start_seconds = seek_seconds;
                strncpy(resume_media_id, media_id, sizeof(resume_media_id) - 1);
                resume_media_id[sizeof(resume_media_id) - 1] = '\0';
                resume_pending = seek_requested = 1;
            }
            break;
        }
        if (action) music_remote_action = MUSIC_REMOTE_NONE;
        if(live && music_radio_ready) {
            __sync_synchronize();
            snprintf(radio_station,sizeof(radio_station),"%s",music_radio_station);
            snprintf(radio_song,sizeof(radio_song),"%s",music_radio_title);
            __sync_synchronize();music_radio_ready=0;
        }
        if(live) {
            unsigned long long now=sceKernelGetSystemTimeWide();
            if(audio_played_blocks!=last_radio_blocks) {last_radio_blocks=audio_played_blocks;radio_progress=now;}
            if(now-radio_progress>30000000ULL)break;
        }
        keep_awake();
        /* A true visualizer fullscreen owns all visible pixels. Do not draw
         * receiver controls between GU frames (including throttled frames). */
        if (fullscreen && !music_visual_active) {
            video_watch_ping("fullscreen spectrum");
            draw_fullscreen_spectrum();
        } else if (!(music_visual_active && fullscreen)) {
            video_watch_ping("music GUI");
            if (tv_ui_active) tv_draw_music(title, fullscreen);
            else lcd_draw_music(title, fullscreen);
        }
        if (!fullscreen || music_visual_active) spectrum_fullscreen_reset();
        if(live)music_caption(radio_station,radio_song,fullscreen);
        else music_caption(current_media_artist[0]?current_media_artist:tr(TXT_MUSIC),current_media_title[0]?current_media_title:title,fullscreen);
        if(!audio_start) {next_preset_tick=sceKernelGetSystemTimeWide()+preset_interval_us();memset(&preset_cuts,0,sizeof(preset_cuts));}
        int hard_cut=0;
        if(audio_start && visual_preset==4 && music_preset_auto && preset_hard_cuts) {
            unsigned char bands[12];for(int i=0;i<12;i++)bands[i]=spectrum_levels[i];
            hard_cut=preset_cut_due(&preset_cuts,bands,sceKernelGetSystemTimeWide());
        }
        if(sequence && music_preset_auto && visual_preset==4 && (music_visual_active || preset_result!=MD_FILE_OK) && audio_start &&
           (hard_cut || (unsigned long long)sceKernelGetSystemTimeWide()>=next_preset_tick)) {
            int index=preset_sequence_next(sequence,music_preset_file,music_preset_auto);
            next_preset_tick=sceKernelGetSystemTimeWide()+preset_interval_us();
            if(index>=0) {
                char path[272];MdFileError error;
                snprintf(path,sizeof(path),"presets/%s",sequence->catalog.names[index]);
                video_watch_ping("automatic preset load");
                int result=md_load_transition(path,hard_cut?0:music_preset_fade_ms,&error);
                if(result==MD_FILE_OK) {
                    strcpy(music_preset_file,sequence->catalog.names[index]);
                    md_preset_duration=(float)music_preset_seconds;
                    preset_result=MD_FILE_OK;
                    if(!music_visual_active) music_visual_active=md_start();
                } else {sequence->rating[index]=-1;next_preset_tick=sceKernelGetSystemTimeWide()+250000;}
            }
        }
        if (music_visual_active && !tvout_video_active && display_output.tv == tv_ui_active) {
            video_watch_ping("music visualization");
            unsigned char bands[SPECTRUM_BANDS];
            int band, level = audio_start ? (vu_left + vu_right)/2 : 0;
            for (band = 0; band < SPECTRUM_BANDS; band++)
                bands[band] = audio_start ? spectrum_levels[band] : 0;
            if(debug_enabled)md_profile_select(visual_preset==6?"Cave field":music_preset_file,tv_ui_active,fullscreen,visual_preset-1);
            int rendered = md_frame(tv_ui_active, fullscreen, bands, level,
                          sceKernelGetSystemTimeWide(), visual_preset-1);
            if (rendered <= 0) {
                md_stop(); music_visual_active = visual_preset = 0;
                if (rendered < 0) {
                    if(sequence && music_preset_auto) {
                        for(int i=0;i<sequence->catalog.count;i++) if(!strcmp(sequence->catalog.names[i],music_preset_file)) sequence->rating[i]=-1;
                        next_preset_tick=sceKernelGetSystemTimeWide()+250000;
                    }
                    preset_error = md_runtime_error;
                    preset_result = preset_error.code;
                    visual_preset = 4;
                    preset_notice_tick = ~0ULL;
                }
                lcd_music_reset(); tv_music_reset();
            }
        }
        if (visual_preset == 4 && preset_result != MD_FILE_OK) {
            unsigned long long tick = tv_ui_active ? tv_music.next_tick : lcd_music.next_tick;
            if (tick != preset_notice_tick) {
                music_preset_notice(&preset_error, fullscreen);
                preset_notice_tick = tick;
            }
        }
        sceCtrlPeekBufferPositive(&pad, 1);
        if(music_visual_active && (pad.Buttons&PSP_CTRL_CROSS) && !(old&PSP_CTRL_CROSS) && !(pad.Buttons&PSP_CTRL_TRIANGLE))
            md_title(subtitle_font,live?radio_station:current_media_artist,live?radio_song:current_media_title[0]?current_media_title:title,sceKernelGetSystemTimeWide(),1);
        if ((pad.Buttons & PSP_CTRL_CIRCLE) && !(old & PSP_CTRL_CIRCLE)) {
            md_stop(); music_visual_active=0;
            int preset_changed=0;
            if(visual_preset==6)music_visual_options(1);
            else preset_changed=music_choose_preset();
            if(preset_changed) {
                preset_result=music_load_selected(&preset_error);
                visual_preset=4; music_saved_visual_preset=4;
                save_playback_settings();
            }
            md_preset_duration=(float)music_preset_seconds;
            if(music_preset_auto && (!sequence || preset_changed)) {
                if(!sequence)sequence=malloc(sizeof(*sequence));
                if(sequence) {video_watch_ping("preset playlist load");preset_sequence_load_selected(sequence,"presets",music_preset_file,(unsigned int)sceKernelGetSystemTimeWide());}
            }
            next_preset_tick=sceKernelGetSystemTimeWide()+preset_interval_us();
            memset(&preset_cuts,0,sizeof(preset_cuts));
            save_playback_settings();
            if(visual_preset && (visual_preset!=4 || preset_result==MD_FILE_OK) && md_start()) music_visual_active=1;
            preset_notice_tick=~0ULL;
            paused=!audio_start;
            lcd_music_reset(); tv_music_reset(); spectrum_fullscreen_reset();
            sceCtrlPeekBufferPositive(&pad,1); old=pad.Buttons;
            continue;
        }
        int cave_flying=0;
        if(visual_preset==6 && music_visual_active) {
            unsigned int shoulders=PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER;
            int both=(pad.Buttons&shoulders)==shoulders;
            int toggle=both && (old&shoulders)!=shoulders && !(pad.Buttons&PSP_CTRL_SELECT);
            int roll=both?0:!!(pad.Buttons&PSP_CTRL_RTRIGGER)-!!(pad.Buttons&PSP_CTRL_LTRIGGER);
            int throttle=!!(pad.Buttons&PSP_CTRL_UP)-!!(pad.Buttons&PSP_CTRL_DOWN);
            cave_flying=md_cave_control(toggle,pad.Lx,pad.Ly,throttle,roll);
        }
        if ((pad.Buttons & PSP_CTRL_START) && !(old & PSP_CTRL_START)) {
            stopped_by_user = 1;
            break;
        }
        if ((pad.Buttons & PSP_CTRL_SELECT) && !(old & PSP_CTRL_SELECT)) {
            if(live) {radio_next_action=2;stopped_by_user=1;break;}
            paused = !paused; audio_start = !paused;
        }
        if (!cave_flying && (pad.Buttons & (PSP_CTRL_UP | PSP_CTRL_DOWN))) {
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
        /* Triangle alone is enough; holding X as before remains harmless. */
        if ((pad.Buttons & PSP_CTRL_TRIANGLE) && !(old & PSP_CTRL_TRIANGLE)) {
            fullscreen = !fullscreen;
            music_saved_fullscreen = fullscreen;
            lcd_music_reset(); tv_music_reset();
        }
        if ((pad.Buttons & PSP_CTRL_SQUARE) && !(old & PSP_CTRL_SQUARE)) {
            md_stop();music_visual_active=0;
            visual_preset = visual_preset == 0 ? 4 : visual_preset == 4 ? 6 : 0;
            if (visual_preset) {
                if (visual_preset != 4 || preset_result == MD_FILE_OK) {
                    music_visual_active = md_start();
                    if (!music_visual_active) visual_preset = 0;
                } else music_visual_active = 0; /* Show the existing preset error. */
            }
            music_saved_visual_preset = visual_preset;
            next_preset_tick=sceKernelGetSystemTimeWide()+preset_interval_us();
            if(visual_preset==4 && music_visual_active) md_begin_preset(0);
            preset_notice_tick = ~0ULL;
            lcd_music_reset(); tv_music_reset();
        }
        old = pad.Buttons;
        sceKernelDelayThread(MUSIC_UI_INPUT_POLL_US);
    }
    music_remote_running = 0;
    video_watch_ping("music stop: close socket");
    audio_running = 0; audio_start = 1;
    if (audio_socket_fd >= 0) sceNetInetShutdown(audio_socket_fd,2);
    video_watch_ping("music stop: join producer");
    sceKernelWaitThreadEnd(audio_thread_id, NULL);
    sceKernelDeleteThread(audio_thread_id);
    if (audio_output_thread_id >= 0) {
        int output_thread_id = audio_output_thread_id;
        video_watch_ping("music stop: join DAC");
        sceKernelWaitThreadEnd(output_thread_id, NULL);
        sceKernelDeleteThread(output_thread_id);
        audio_output_thread_id = -1;
    }
    audio_queue_destroy();
    /* An EOF racing a terminal remote command must not trigger autoplay. */
    /* Producer and DAC have joined; stop reusing the last song's waveform. */
    { const short silence[2]={0,0};visualization_pcm_publish(silence,1); }
    if (music_remote_action >= MUSIC_REMOTE_STOP) stopped_by_user = 1;
    video_watch_ping("music stop: join remote");
    music_remote_stop();
    video_watch_ping("music stop: GU");
    free(sequence);
    if(debug_enabled) {
        char profile_text[1024];
        for(int i=0;md_profile_report(i,profile_text,sizeof(profile_text));i++)
            video_watch_write(profile_text,0);
    }
    /* The MP3 worker deliberately treats HTTP EOF as a neutral shutdown so
     * transient WLAN failures do not masquerade as decoder faults.  Compare
     * the DAC clock to ffprobe's duration here to classify a genuine song
     * end, just as video uses its rendered-frame clock. */
    if(offline_music && offline_music_eof && !stopped_by_user && audio_state>=15)
        playback_reached_end=1;
    if (!offline_music && !live && !stopped_by_user && current_duration_seconds > 0.0f && audio_state >= 15 &&
        remote_result >= 0 &&
        stream_start_seconds + (float)audio_played_blocks * (float)audio_dac_samples / (float)PSP_AUDIO_SAMPLE_RATE >= current_duration_seconds - 2.0f)
        playback_reached_end = 1;
    if(!offline_music && !live && !stopped_by_user && !seek_requested && !video_file_direction) {
        music_network_failed=audio_state==-12 || audio_state==-13 || audio_state==-14 ||
            audio_state==-17 || audio_state==-28 ||
            (audio_state>=15 && !playback_reached_end && current_duration_seconds>0);
        if(music_network_failed) {
            playback_reached_end=0;
            playback_position_ms=stream_start_seconds*1000+(int)((unsigned long long)
                audio_played_blocks*audio_dac_samples*1000/PSP_AUDIO_SAMPLE_RATE);
        }
    }
    music_transition=music_visual_active && !live && remote_result>=0 && audio_state>=0 &&
        (playback_reached_end || video_file_direction || (resume_pending&&seek_requested));
    if(!music_transition){md_stop();music_visual_active=0;}
    music_ui_restore_priority(previous_ui_priority);
    video_watch_stop();
    if(live && !stopped_by_user && remote_result>=0)radio_next_action=1;
    return remote_result < 0 ? remote_result : audio_state < 0 ? audio_state : 0;
}

/* Every retry joins the old workers and releases the codec first. The waiting
 * screen owns no audio socket, PCM queue or GU list. Never autoplay a station. */
#include "playback_recovery.h"
static int play_audio(const char *media_id,const char *title) {
    menu_art_select(""); /* No decorative image RAM or requests during playback. */
    int result;
    /* Keep the current profile across track changes/reconnects. The next
     * active renderer selects its profile; only a real browser return idles. */
    do {
        power_music=1;
        result=play_audio_once(media_id,title);
        power_music=0;scePowerTick(PSP_POWER_TICK_ALL);
        if(!radio_is_live(media_id) || !radio_next_action)return result;
        int paused=radio_next_action==2;
        /* Radio pause releases the decoder. Report the waiting screen, not
         * the last playing state, through the existing remote worker. */
        plex_paused=paused;plex_started=paused;
        unsigned long long retry=sceKernelGetSystemTimeWide()+5000000ULL;
        unsigned int old=~0U;
        lcd_music_reset();tv_music_reset();
        const char *message=tr(paused?TXT_RADIO_PAUSED:TXT_RADIO_RECONNECT);
        if(tv_ui_active)tv_draw_music(message,0);else lcd_draw_music(message,0);
        if(music_remote_start()<0)return result<0?result:-1;
        while(1) {
            SceCtrlData pad;keep_awake();sceCtrlPeekBufferPositive(&pad,1);
            if(comfort_expired()){music_remote_stop();return 0;}
            int action=music_remote_action;
            if(action==MUSIC_REMOTE_STOP || action==MUSIC_REMOTE_PLAY ||
               ((pad.Buttons & (PSP_CTRL_START|PSP_CTRL_CIRCLE)) & ~old)) {
                music_remote_stop();return 0;
            }
            if(action==MUSIC_REMOTE_PAUSE && !paused) {
                plex_paused=plex_started=1;
                paused=1;lcd_music_reset();tv_music_reset();
                if(tv_ui_active)tv_draw_music(tr(TXT_RADIO_PAUSED),0);else lcd_draw_music(tr(TXT_RADIO_PAUSED),0);
            }
            if(action==MUSIC_REMOTE_RESUME ||
               ((pad.Buttons & (PSP_CTRL_SELECT|PSP_CTRL_CROSS|PSP_CTRL_SQUARE)) & ~old) ||
               (!paused && (unsigned long long)sceKernelGetSystemTimeWide()>=retry))break;
            music_remote_action=0;old=pad.Buttons;sceKernelDelayThread(20000);
        }
        music_remote_stop();
        if(audio_played_blocks*audio_dac_samples/(unsigned int)PSP_AUDIO_SAMPLE_RATE>=30)recovery_failures=0;
        if(!paused)recovery_failures++;
        int connected=playback_recovery_associate();
        if(connected==-5)return 0;
        stream_start_seconds=0;resume_pending=seek_requested=0;
        sceKernelDelayThread(250000);
    } while(1);
}

static int play_h264(const char *media_id) {
    timed_network_failed=0;
    music_transition_end();
    menu_art_select("");
    plex_report_begin(media_id);
    video_controls.visible=video_controls.saved=0;
    video_controls.selected=3;
    video_file_direction=0;
    int frames = 0, result = 0, buffered = 0, duration = 0, tail_clock = 0;
    int stopped_by_user=0;
    int prepared = 0, trace_start_seconds = stream_start_seconds;
    int watch_started = 0;
    int video_only_origin = 0;
    int audio_thread_id = -1;
    TimedPacket current = {0}, next = {0};
    unsigned long long video_only_tick = 0, pause_tick = 0;
    unsigned long long next_volume_repeat_tick = 0;
    unsigned int previous_buttons = 0;
    int paused = 0;
    PtsDrainClock drain_clock={0};
    if(offline_active) {offline_prepare_seek(stream_start_seconds);stream_start_seconds=offline_seek_ms/1000;}
    playback_reached_end = 0;
    playback_paused = 0;
    playback_position_ms = stream_start_seconds * 1000;
    vu_left = vu_right = vu_display_left = vu_display_right = 0;
    /* Keep the established firmware module order. Subtitle preparation is
     * independent of the transcode and may be slow for a cold remote source. */
    result = load_video_modules();
    if (result < 0) return result;
    if (hardware_runtime_result != 0) { video_step = "Media-Engine Bridge"; return hardware_runtime_result; }
    video_step = "Hardware-AVC";
    hardware_decoder_ready = 0;
    hardware_decoder_frames = 0;
    video_staging = NULL;
    sync_trace_reset();
    /* Preparation is still a menu operation, not a video framebuffer. Keep
     * native LCD/TV UI visible and cancellable until its HTTP worker exits. */
    int subtitle_tv_profile=tvout_load_manager()>=0 && pspDveMgrCheckVideoOut()==2;
    result=prepare_client_subtitles(media_id,subtitle_tv_profile);
    if(result<0) {
        subtitle_release();video_step="Subtitles";
        if(media_request_transient && result!=MEDIA_REQUEST_CANCELLED) {
            timed_network_failed=1;return -1320;
        }
        return result==MEDIA_REQUEST_CANCELLED?0:result;
    }
    tvout_video_active = tvout_begin_video() == 0;
    if(offline_active && offline_profile_tv!=tvout_video_active) {
        video_step=tr(TXT_DOWNLOAD_PROFILE);
        subtitle_release();
        if(tvout_video_active)tvout_end_video();
        tvout_video_active=0;
        return -1401;
    }
    if (tvout_video_active) memset((void *)0x44000000, 0, TVOUT_STRIDE * 480 * 4);
    playback_clock(video_cpu_mhz);
    /* PGS sprites are comparatively large.  The LCD path caches and fetches
     * them on demand, which is acceptable at 480x272 but stalls the video
     * clock in native TV mode.  Let FFmpeg composite them before the stream
     * instead; text cues remain a zero-latency PSP overlay. */
    if (tvout_video_active && bitmap_client_side) {
        if (bitmap_cues) free(bitmap_cues);
        bitmap_cues = NULL;
        bitmap_cue_count = 0;
        bitmap_client_side = 0;
        bitmap_loaded_cue = -1;
    }
    h264_hw_set_output_layout(tvout_video_active ? TVOUT_STRIDE : VIDEO_STRIDE,
                              tvout_video_active ? 480 : VIDEO_HEIGHT,
                              tvout_video_active ? 5 : 4);
    strncpy(audio_media_id, media_id, sizeof(audio_media_id) - 1);
    audio_media_id[sizeof(audio_media_id) - 1] = '\0';
    audio_start = 0;
    audio_clock_started = 0;
    video_first_presented = 0;
    /* One muxed stream supplies both codecs. Keep the DAC gated until the
     * first decoded frame is actually queued for display; then follow PTS. */
    audio_running = 1;
    audio_state = 0;
    audio_prefill_target = AUDIO_PREFILL_BLOCKS;
    audio_dac_samples = AUDIO_BLOCK_SAMPLES;
    audio_queue_read = audio_queue_write = audio_played_blocks = 0;
    audio_current_timestamp_ms = 0;
    memset(audio_block_timestamp_ms, 0, sizeof(audio_block_timestamp_ms));
    audio_queue_primed = audio_blocks_published = 0;

    timed_active = timed_running = 1;
    timed_eof = timed_error = timed_audio_done = timed_playing = 0;
    timed_video_blocked=timed_audio_waiting=0;
    timed_has_audio = -1;
    timed_video_origin_set = timed_position_ms = 0;
    timed_video.free = timed_video.ready = timed_audio.free = timed_audio.ready = -1;
    timed_reader_id = audio_output_thread_id = -1;
    if (timed_queue_init(&timed_video) < 0 || timed_queue_init(&timed_audio) < 0) {
        result = -1321; goto done;
    }
    codec_sema = sceKernelCreateSema("PSPStreamerME", 0, 1, 1, NULL);
    if (codec_sema < 0) { result = codec_sema; goto done; }
    snprintf(timed_request, sizeof(timed_request),
        "GET /api/transcode/%s?container=flv&profile=%s&audio=%d&subtitle=%d&audio_quality=%s&video_fps=%s&start=%d HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n%s\r\n",
        media_id, tvout_video_active ? "tv" : PSP_STREAMER_PROFILE, selected_audio_track,
        (subtitle_client_side || bitmap_client_side) ? -1 : selected_subtitle_track,
        audio_quality_name(), selected_video_fps ? "24000/1001" : "20", stream_start_seconds, server_host, server_auth_header);
    video_step = "FLV reader worker";
    timed_reader_id = sceKernelCreateThread("PSPStreamerFLV", timed_reader, 0x20, server_https?0x10000:0x5000, 0, NULL);
    if (timed_reader_id < 0) { result = timed_reader_id; goto done; }
    if (sceKernelStartThread(timed_reader_id, 0, NULL) < 0) {
        sceKernelDeleteThread(timed_reader_id); timed_reader_id = -1; result = -1321; goto done;
    }
    remote_control_action = 0;
    remote_control_seek_seconds = -1;
    remote_control_running = 1;
    remote_control_thread_id = offline_active ? -1 : sceKernelCreateThread("PSPStreamerRemote", remote_control_thread, server_https?0x40:0x20, server_https?0x10000:0x3000, 0, NULL);
    result = remote_control_thread_id < 0 ? remote_control_thread_id :
        sceKernelStartThread(remote_control_thread_id, 0, NULL);
    if (offline_active) {result=0;remote_control_running=0;}
    if (result < 0) {
        if (remote_control_thread_id >= 0) sceKernelDeleteThread(remote_control_thread_id);
        remote_control_thread_id = -1;
        remote_control_running = 0;
        video_step = "Remote worker";
        goto done;
    }
    video_step = "FLV/PTS stream";
    result = video_watch_start(0);
    if (result < 0) { video_step = "Diagnostic file"; goto done; }
    watch_started = 1;
    while (1) {
        SceCtrlData pad;
        video_watch_ping("video loop");
        if(comfort_expired()){stopped_by_user=1;result=frames;break;}
        playback_clock(video_cpu_mhz);
        plex_position_ms=playback_position_ms;
        plex_paused=paused;
        plex_started=video_first_presented;
        keep_awake();
        sceCtrlPeekBufferPositive(&pad, 1);
        if (remote_control_action) {
            int action = remote_control_action;
            remote_control_action = 0;
            if (action == 1 || action == 2) {
                paused = action == 1;
                playback_paused = paused;
                audio_start = paused ? 0 : buffered;
            } else if (action == 3 || action == 4) { stopped_by_user=1; result = frames; break; }
        }
        if (remote_control_seek_seconds >= 0) {
            stream_start_seconds = remote_control_seek_seconds;
            remote_control_seek_seconds = -1;
            strncpy(resume_media_id, media_id, sizeof(resume_media_id) - 1);
            resume_media_id[sizeof(resume_media_id) - 1] = '\0';
            resume_pending = 1;
            seek_requested = 1;
            result = frames;
            break;
        }
        if (pad.Buttons & PSP_CTRL_START) { stopped_by_user=1; result = frames; break; }
        {
            unsigned int pressed=pad.Buttons & ~previous_buttons;
            int was_visible=video_controls.visible;
            if((pressed & PSP_CTRL_SELECT) && (video_fullscreen || tvout_video_active || was_visible) && video_first_presented) {
                if(was_visible) video_controls_hide();
                else video_controls_show(tvout_video_active,paused);
                /* Select opens controls without changing the playback clock. */
                previous_buttons |= PSP_CTRL_SELECT;
            }
            if(was_visible && (pressed & PSP_CTRL_CIRCLE)) {
                video_controls_hide(); previous_buttons |= PSP_CTRL_CIRCLE;
            }
            if(video_controls.visible) {
                if(pressed & PSP_CTRL_LEFT) video_controls.selected=(video_controls.selected+6)%7;
                if(pressed & PSP_CTRL_RIGHT) video_controls.selected=(video_controls.selected+1)%7;
                if((pressed & PSP_CTRL_CROSS) && !(pad.Buttons & PSP_CTRL_TRIANGLE)) {
                    static const int deltas[]={0,-30,-10,0,10,30,0};
                    if(video_controls.selected==0 || video_controls.selected==6) {
                        video_file_direction=video_controls.selected==0?-1:1;
                        result=frames; break;
                    } else if(video_controls.selected==3) {
                        paused=!paused; playback_paused=paused; audio_start=paused?0:buffered;
                    } else {
                        stream_start_seconds = (offline_active ? playback_position_ms/1000 :
                            stream_start_seconds+timed_position_ms/1000)+deltas[video_controls.selected];
                        if(stream_start_seconds<0) stream_start_seconds=0;
                        strncpy(resume_media_id,media_id,sizeof(resume_media_id)-1);
                        resume_media_id[sizeof(resume_media_id)-1]=0;
                        resume_pending=seek_requested=1; result=frames; break;
                    }
                }
                if(pressed) video_controls_draw(paused);
            }
        }
        if ((pad.Buttons & PSP_CTRL_SELECT) && !(previous_buttons & PSP_CTRL_SELECT)) {
            paused = !paused;
            playback_paused = paused;
            audio_start = paused ? 0 : buffered;
            if (tvout_video_active) tvout_playback_hud(hardware_decoder_frames, paused);
            else playback_hud(hardware_decoder_frames, paused);
            sceDisplaySetFrameBuf((void *)0x04000000,
                                  tvout_video_active ? TVOUT_STRIDE : VIDEO_STRIDE,
                                  PSP_DISPLAY_PIXEL_FORMAT_8888,
                                  PSP_DISPLAY_SETBUF_NEXTVSYNC);
        }
        if ((pad.Buttons & PSP_CTRL_TRIANGLE) && !(previous_buttons & PSP_CTRL_TRIANGLE)) {
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
            stream_start_seconds = (offline_active ? playback_position_ms/1000 :
                stream_start_seconds+timed_position_ms/1000)+delta;
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
        if (paused) { if (!pause_tick) pause_tick = sceKernelGetSystemTimeWide(); sceKernelDelayThread(75000); continue; }

        if (pause_tick) {
            unsigned long long elapsed=sceKernelGetSystemTimeWide()-pause_tick;
            video_only_tick+=elapsed;
            if(drain_clock.active)drain_clock.tick+=elapsed;
            pause_tick=0;
        }
        if (timed_error || audio_state < 0) {
            result = timed_error ? timed_error : audio_state;
            video_step = timed_error ? timed_error_step : "MP3";
            break;
        }
        if (timed_has_audio == 1 && audio_thread_id < 0) {
            audio_thread_id = sceKernelCreateThread("PSPStreamerAudio", audio_thread, 0x18, 0x4000, 0, NULL);
            if (audio_thread_id < 0) { result = audio_thread_id; break; }
            if (sceKernelStartThread(audio_thread_id, 0, NULL) < 0) {
                sceKernelDeleteThread(audio_thread_id); audio_thread_id = -1; result = -1321; break;
            }
        }
        if (!current.data) timed_get(&timed_video, &current);
        if (current.data && !next.data) timed_get(&timed_video, &next);
        if (current.data && !timed_video_origin_set) {
            timed_video_origin = current.pts; timed_video_origin_set = 1;
        }
        if (current.data && next.data) {
            duration = next.pts - current.pts;
            if (duration <= 0) { result = -1322; break; }
        }
        if (!buffered) {
            if (!current.data || (!next.data && !timed_eof) || timed_has_audio < 0 ||
                (timed_has_audio && !audio_queue_primed)) {
                if (timed_eof && !current.data) {
                    /* The reader can fail during timed_get's semaphore wait,
                     * after the error check above. Do not hide its error as
                     * "no H.264 frames", particularly for missing local files. */
                    result = timed_error ? timed_error : -1306;
                    if (timed_error) video_step = timed_error_step;
                    break;
                }
                sceKernelDelayThread(2000); continue;
            }
            /* Initialise AVC before starting the DAC, keeping the established
             * MPEG/ME module order. No decoder startup delay enters A/V time. */
            video_watch_ping("ME lock for init");
            if (!codec_enter()) { result = -1324; break; }
            video_watch_ping("AVC init");
            result = h264_hw_init_avcc((const AvcPacket *)current.data, current.size);
            codec_leave();
            if (result < 0) { video_step = h264_hw_last_step(); break; }
            hardware_decoder_ready = 1;
            video_staging_bytes = (tvout_video_active ? TVOUT_STRIDE * 480 : VIDEO_STRIDE * VIDEO_HEIGHT) * 4;
            video_staging = memalign(64, video_staging_bytes);
            if (!video_staging) { result = -1325; video_step = "Video staging RAM"; break; }
            memset(video_staging, 0, video_staging_bytes);
            subtitle_parse_prepared_response();
            audio_start = 1; buffered = timed_playing = 1;
            video_only_tick = sceKernelGetSystemTimeWide();
            video_only_origin = timed_video_origin;
        }
        if (!current.data) {
            if (timed_eof && (!timed_has_audio || timed_audio_done)) {
                playback_reached_end = 1; result = frames; break;
            }
            sceKernelDelayThread(2000); continue;
        }
        if (!prepared) {
            prepared = prepare_timed_video(&current);
            if (prepared < 0) { result = prepared; break; }
            if (!prepared) {
                free(current.data); current = next; memset(&next, 0, sizeof(next));
                continue;
            }
        }
        {
            int sync;
            unsigned int copy_us = 0;
            unsigned long long clock_now=sceKernelGetSystemTimeWide();
            int audio_end=(int)audio_current_timestamp_ms+audio_dac_samples*1000/44100;
            if(timed_has_audio && !timed_audio_done)
                pts_drain_update(&drain_clock,clock_now,video_first_presented && audio_clock_started,
                    (int)audio_current_timestamp_ms,audio_end,timed_video_blocked,timed_audio_waiting,
                    timed_audio.read==timed_audio.write,audio_played_blocks==audio_blocks_published);
            if (timed_has_audio && timed_audio_done && !tail_clock) {
                video_only_tick = clock_now;
                video_only_origin = audio_end;
                if(timed_video_origin+timed_position_ms>video_only_origin)
                    video_only_origin=timed_video_origin+timed_position_ms;
                if(drain_clock.active && pts_drain_time(&drain_clock,clock_now)>video_only_origin)
                    video_only_origin=pts_drain_time(&drain_clock,clock_now);
                drain_clock.active=0;
                tail_clock = 1;
            }
            sync = pts_presentation_status(prepared, video_first_presented,
                timed_has_audio && !timed_audio_done && !drain_clock.active, audio_clock_started,
                (int)audio_current_timestamp_ms, current.pts, duration,
                drain_clock.active?pts_drain_time(&drain_clock,clock_now):
                    video_only_origin + (int)((clock_now - video_only_tick) / 1000ULL));
            if (!sync) {
                sync_trace_record(current.pts, 0, 0);
                sceKernelDelayThread(2000); continue;
            }
            if (sync == 1) {
                /* The complete picture (CSC and subtitles included) is now
                 * ready. Refresh the decision at VBlank before touching VRAM. */
                video_watch_ping("VBlank");
                sceDisplayWaitVblankStart();
                sync = pts_presentation_status(prepared, video_first_presented,
                    timed_has_audio && !timed_audio_done && !drain_clock.active, audio_clock_started,
                    (int)audio_current_timestamp_ms, current.pts, duration,
                    drain_clock.active?pts_drain_time(&drain_clock,sceKernelGetSystemTimeWide()):
                        video_only_origin + (int)((sceKernelGetSystemTimeWide() - video_only_tick) / 1000ULL));
                if (!sync) continue;
                if (sync == 1) {
                    unsigned long long copy_start = debug_enabled ? sceKernelGetSystemTimeWide() : 0;
                    memcpy((void *)0x44000000, video_staging, video_staging_bytes);
                    video_controls_present(paused);
                    result = sceDisplaySetFrameBuf((void *)0x04000000,
                        tvout_video_active ? TVOUT_STRIDE : VIDEO_STRIDE,
                        PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_IMMEDIATE);
                    if(debug_enabled) copy_us = (unsigned int)(sceKernelGetSystemTimeWide() - copy_start);
                    if (result < 0) { video_step = "Video display"; break; }
                    if (!video_first_presented) {
                        video_only_tick = sceKernelGetSystemTimeWide();
                        video_only_origin = current.pts;
                    }
                    video_first_presented = 1;
                }
            }
            sync_trace_record(current.pts, sync, copy_us);
            timed_position_ms = current.pts - timed_video_origin;
            if (timed_position_ms < 0) timed_position_ms = 0;
            playback_position_ms = offline_active ? current.pts : stream_start_seconds * 1000 + timed_position_ms;
            frames += prepared;
            prepared = 0;
            free(current.data); current = next; memset(&next, 0, sizeof(next));
        }
    }
    if (result < 0 && frames && !seek_requested) {
        stream_start_seconds += timed_position_ms / 1000;
        if (stream_start_seconds > 2) stream_start_seconds -= 2;
        strncpy(resume_media_id, media_id, sizeof(resume_media_id) - 1);
        resume_media_id[sizeof(resume_media_id) - 1] = 0; resume_pending = 1;
    }
done:
    video_watch_ping("stop: close FLV socket");
    timed_running = 0;
    audio_running = 0; audio_start = 1;
    video_first_presented = 0;
    remote_control_running = 0;
    if (timed_socket >= 0) {
        sceNetInetShutdown(timed_socket,2);
    }
    if (timed_reader_id >= 0) {
        video_watch_ping("stop: join FLV reader");
        sceKernelWaitThreadEnd(timed_reader_id, NULL);
        sceKernelDeleteThread(timed_reader_id); timed_reader_id = -1;
    }
    if (timed_error) {
        video_watch_ping("stop: stream error report");
        if (stream_diag_save(timed_error) < 0)
            video_watch_write("stream error report: write failed\n",0);
    }
    if (audio_thread_id >= 0) {
        video_watch_ping("stop: join MP3 decoder");
        sceKernelWaitThreadEnd(audio_thread_id, NULL);
        sceKernelDeleteThread(audio_thread_id);
    }
    if (audio_output_thread_id >= 0) {
        video_watch_ping("stop: join DAC");
        sceKernelWaitThreadEnd(audio_output_thread_id, NULL);
        sceKernelDeleteThread(audio_output_thread_id); audio_output_thread_id = -1;
    }
    if (remote_control_thread_id >= 0) {
        video_watch_ping("stop: join remote HTTP");
        sceKernelWaitThreadEnd(remote_control_thread_id, NULL);
        sceKernelDeleteThread(remote_control_thread_id); remote_control_thread_id = -1;
    }
    free(current.data); free(next.data);
    free(video_staging); video_staging = NULL;
    playback_draw_target = (u32 *)0x44000000;
    if (codec_sema >= 0) { sceKernelDeleteSema(codec_sema); codec_sema = -1; }
    timed_queue_destroy(&timed_video); timed_queue_destroy(&timed_audio);
    timed_active = 0;
    audio_queue_destroy();
    video_watch_ping("stop: MPEG shutdown");
    h264_hw_shutdown(); subtitle_release();
    video_watch_ping("stop: trace/display restore");
    sync_trace_save(tvout_video_active, result, trace_start_seconds);
    if (tvout_video_active) tvout_end_video();
    tvout_video_active = 0;
    video_watch_stop();
    if(debug_enabled && watch_started) {
        char outcome[224];
        snprintf(outcome,sizeof(outcome),
            "playback outcome: result=%d natural_end=%d resume_pending=%d seek_requested=%d direction=%d start_seconds=%d position_ms=%d\n",
            result,playback_reached_end,resume_pending,seek_requested,video_file_direction,
            trace_start_seconds,playback_position_ms);
        video_watch_write(outcome,0);
    }
    scePowerTick(PSP_POWER_TICK_ALL);
    if(stopped_by_user) {timed_network_failed=0;return frames;}
    if (result < 0) return result;
    if (!frames) video_step = "no H.264 frames";
    return frames ? frames : -1306;
}

static int comfort_play_audio(const char *id,const char *name) {
    playback_recovery_reset();
    comfort_timer_stopped=0;
    int result;
    do {
        int recovery_start=stream_start_seconds;
        result=play_audio(id,name);
        if(offline_active || radio_is_live(id) || !music_network_failed)break;
        if(playback_position_ms-recovery_start*1000>=30000)recovery_failures=0;
        if(!playback_reconnect_wait()) {playback_recovery_cancel();result=0;break;}
        stream_start_seconds=playback_recovery_position(playback_position_ms);
        resume_pending=seek_requested=0;
    } while(1);
    comfort_finished(id,name,1,offline_active,result);
    if(comfort_timer_stopped)video_file_direction=0;
    if(!playback_reached_end && !video_file_direction && !seek_requested)music_transition_end();
    return result;
}
static int comfort_play_video(const char *id) {
    playback_recovery_reset();
    comfort_timer_stopped=0;playback_reached_end=0;
    playback_position_ms=stream_start_seconds*1000;
    int result,decoder_failures=0;
    do {
        int recovery_start=stream_start_seconds;
        result=play_h264(id);
        if(offline_active || seek_requested || video_file_direction)break;
        int decoder=playback_decoder_retry(result,video_step,
            playback_position_ms-recovery_start*1000,&decoder_failures);
        if(decoder<0) {video_step=tr(TXT_STREAM_DECODER_FAILED);break;}
        if(!decoder && (result!=-1320 || !timed_network_failed))break;
        if(playback_position_ms-recovery_start*1000>=30000)recovery_failures=0;
        if(!playback_recover_wait(!decoder)) {playback_recovery_cancel();result=0;break;}
        stream_start_seconds=playback_recovery_position(playback_position_ms);
        resume_pending=seek_requested=0;
    } while(1);
    comfort_finished(id,current_media_name,0,offline_active,result);
    if(comfort_timer_stopped)video_file_direction=0;
    return result;
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

static int load_media_metadata(const char *media_id) {
    current_media_plex=!strncmp(media_id,"plex.",5) || !strncmp(media_id,"jellyfin.",9);
    char path[ID_SIZE + 32], duration[24];
    int result;
    snprintf(path, sizeof(path), "/api/metadata/%s", media_id);
    current_duration_seconds = 0.0f;
    provider_resume_seconds=-1;
    current_media_name[0]=0;
    current_media_title[0]=current_media_artist[0]=current_media_album[0]=0;
    result = media_request_get(path, response, sizeof(response), 60000, 0);
    audio_track_count = subtitle_track_count = 0;
    if (result < 0) {
        video_step="Metadata";
        if(result==MEDIA_REQUEST_CANCELLED)snprintf(status,sizeof(status),"%s",tr(TXT_LIBRARY_CANCELLED));
        else snprintf(status,sizeof(status),tr(TXT_SERVER_ERROR),result);
        return result;
    }
    json_value(response,"name",current_media_name,sizeof(current_media_name));
    json_value(response,"title",current_media_title,sizeof(current_media_title));
    json_value(response,"artist",current_media_artist,sizeof(current_media_artist));
    json_value(response,"album",current_media_album,sizeof(current_media_album));
    parse_stream_tracks("\"a\":[", audio_tracks, &audio_track_count);
    parse_stream_tracks("\"s\":[", subtitle_tracks, &subtitle_track_count);
    current_duration_seconds = json_value(response, "d", duration, sizeof(duration)) ? (float)atof(duration) : 0.0f;
    const char *resume_value=strstr(response,"\"resume\":");
    if(resume_value)provider_resume_seconds=atoi(resume_value+9);
    if (audio_track_count && selected_audio_track >= audio_track_count) selected_audio_track = 0;
    if (!audio_track_count) selected_audio_track = 0;
    if (selected_subtitle_track >= subtitle_track_count) selected_subtitle_track = -1;
    /* Do not prefetch subtitle payloads here.  In particular, a first PGS
     * selection may require mkvextract to build its persistent server cache,
     * which is legitimate work but can take tens of seconds on an SMB disk.
     * Keeping this phase metadata-only guarantees that the options dialog
     * stays responsive; play_h264() prepares the selected overlay only once
     * the user has actually committed to starting the video. */
    return 0;
}

static int json_integer(const char *from, const char *key, int fallback) {
    char needle[24];
    const char *value;
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    value = strstr(from, needle);
    return value ? atoi(value + strlen(needle)) : fallback;
}

#include "browser_remote.h"

/* Consume background replies only while the library is idle. */
static int remote_poll_play(char *media_id, size_t media_id_size, int *audio,
                            int *subtitle, int *is_audio, int *start_seconds) {
    int sequence = remote_control_sequence;
    char action[16], kind[16];
    const char *reply = browser_remote_poll(sequence), *field;
    if (!reply || !json_value(reply, "action", action, sizeof(action))) return 0;
    if (remote_state_reset(reply, &sequence)) return 0;
    field = strstr(reply, "\"seq\":");
    if (field) remote_control_sequence = sequence = atoi(field + 6);
    if (strcmp(action, "play") || !json_value(reply, "id", media_id, media_id_size)) return 0;
    remote_control_sequence = sequence;
    *audio = json_integer(reply, "audio", 0);
    *subtitle = json_integer(reply, "subtitle", -1);
    {
        char setting[20];
        int i;
        static const char *qualities[] = {"96k", "128k", "160k", "v6", "v5", "v4", "v3"};
        if (json_value(reply, "audio_quality", setting, sizeof(setting)))
            for (i = 0; i < 7; i++) if (!strcmp(setting, qualities[i])) selected_audio_quality = i;
        if (json_value(reply, "video_fps", setting, sizeof(setting)))
            selected_video_fps = !strcmp(setting, "24000/1001");
    }
    *start_seconds = json_integer(reply, "start", 0);
    *is_audio = json_value(reply, "kind", kind, sizeof(kind)) && !strcmp(kind, "audio");
    return 1;
}

static void subtitle_page_prefetch(unsigned long long *retry) {
    if(!subtitle_pages_live || subtitle_page_ready || !remote_control_running)return;
    unsigned long long now=sceKernelGetSystemTimeWide();
    if(now<*retry)return;
    __sync_synchronize();
    int bank=subtitle_page_bank,offset=subtitle_pages[bank].next;
    if(offset<0 || (long long)subtitle_page_position+30000<subtitle_pages[bank].until)return;
    char path[ID_SIZE+128];
    snprintf(path,sizeof(path),"/api/subtitles/%s?track=%d&timebase=ms&page=1&offset=%d",
             audio_media_id,selected_subtitle_track,offset);
    /* This worker already owns the control-plane TLS slot. Cached cue pages
     * use it between polls; no new thread, socket pool or render-path HTTP.
     * response is no longer used by the render thread for paged text. */
    int result=remote_http_get_budget(path,response,sizeof(response),&remote_control_running,10000);
    if(!remote_control_running)return;
    SubtitlePage *page=subtitle_pages+(1-bank);
    if(result<0 || !subtitle_page_parse(response,page) || page->offset!=offset) {
        subtitle_page_failures++;*retry=sceKernelGetSystemTimeWide()+2000000ULL;return;
    }
    __sync_synchronize();subtitle_page_ready=1;*retry=0;
}

static int remote_control_thread(SceSize args, void *argp) {
    int sequence = remote_control_sequence;
    unsigned long long subtitle_retry=0;
    (void)args; (void)argp;
    while (remote_control_running) {
        char path[ID_SIZE+192], reply[2048], action[16];
        char *field;
        if (remote_control_action || remote_control_seek_seconds >= 0) {
            sceKernelDelayThread(10000); continue;
        }
        plex_report_path(path, sizeof(path), sequence, 0);
        if (remote_http_get(path, reply, sizeof(reply), &remote_control_running) >= 0 &&
            remote_control_running &&
            json_value(reply, "action", action, sizeof(action))) {
            if (remote_state_reset(reply, &sequence)) continue;
            field = strstr(reply, "\"seq\":");
            if (!field || atoi(field + 6) <= sequence) {
                subtitle_page_prefetch(&subtitle_retry);
                sceKernelDelayThread(500000); continue;
            }
            if (remote_control_running && field && atoi(field + 6) > sequence &&
                !strcmp(action, "play")) {
                /* Do not consume Play: exit through normal video teardown,
                 * then let the idle dispatcher load the new file/kind. */
                remote_control_action = 4;
                break;
            }
            if (field) remote_control_sequence = sequence = atoi(field + 6);
            if (!strcmp(action, "pause")) remote_control_action = 1;
            else if (!strcmp(action, "resume")) remote_control_action = 2;
            else if (!strcmp(action, "stop")) remote_control_action = 3;
            else if (!strcmp(action, "seek")) {
                int seconds = json_integer(reply, "seconds", -1);
                if (seconds >= 0 && seconds <= 86400) remote_control_seek_seconds = seconds;
            }
        }
        if(!remote_control_action && remote_control_seek_seconds<0)
            subtitle_page_prefetch(&subtitle_retry);
        sceKernelDelayThread(500000);
    }
    plex_report_stop(sequence);
    return 0;
}

static void gui_library_shell(const char *section);

static void media_wait_draw(int subtitles,unsigned int seconds,int cancelling) {
    if(music_transition && !cancelling){music_transition_frame();return;}
    music_transition_end();
    const char *label=tr(cancelling?TXT_NETWORK_STOPPING:
        subtitles?TXT_PREPARING_SUBTITLES:TXT_LOADING_TRACKS);
    snprintf(status,sizeof(status),tr(TXT_PREPARATION_WAIT),seconds);
    if(tv_ui_active) {tv_draw_view(TV_VIEW_LOADING,0,0,0,label,0);return;}
    gui_library_shell(tr(TXT_PREPARING_MEDIA));
    gui_text(38,47,0x0000D8FF,"%s",tr(TXT_PREPARING_MEDIA));
    gui_text(38,76,0x00FFFFFF,"%s",label);
    gui_text(38,116,0x008A9BAA,"%s",status);
    gui_text(376,47,0x00FFB000,"%s",tr(TXT_PLEASE_WAIT));
}

static void show_metadata_loading(void) {
    if (tv_ui_active) { tv_draw_view(TV_VIEW_LOADING, 0, 0, 0, NULL, 0); return; }
    gui_library_shell(tr(TXT_PREPARING_MEDIA));
    gui_text(38, 47, 0x0000D8FF, "%s", tr(TXT_READING_MEDIA));
    gui_text(38, 76, 0x00FFFFFF, "%s", tr(TXT_LOADING_TRACKS));
    gui_text(38, 96, 0x008A9BAA, "%s", tr(TXT_SOURCE_WAKING));
    /* Start at the panel edge; the compact glyph itself already carries its
     * own tiny left bearing, so an extra character-cell offset reads as a
     * spurious leading blank on the PSP LCD. */
    gui_text(376, 47, 0x00FFB000, "%s", tr(TXT_PLEASE_WAIT));
    gui_text(376, 76, 0x008A9BAA, "%s", tr(TXT_NO_VIDEO));
    gui_text(376, 87, 0x008A9BAA, "%s", tr(TXT_STREAM_HAS));
    gui_text(376, 98, 0x008A9BAA, "%s", tr(TXT_STARTED));
}

static void parse_library(void) {
    char *cursor;
    current_parent_path[0]=0;
    json_value(response,"parent",current_parent_path,sizeof(current_parent_path));
    item_count = 0;
    cursor = strstr(response, "\"folders\":[");
    if (cursor) cursor = strchr(cursor, '[') + 1;
    while (cursor && item_count < MAX_ITEMS) {
        char *object = strchr(cursor, '{');
        if (!object || !json_value(object, "name", items[item_count].title, TITLE_SIZE)) break;
        if (!json_value(object, "path", items[item_count].value, ID_SIZE)) break;
        if(!strcmp(items[item_count].value,":radio:"))snprintf(items[item_count].title,TITLE_SIZE,"%s",tr(TXT_RADIO));
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
    if(!strncmp(current_path,":plex:",6) || !strncmp(current_path,":jellyfin:",10) ||
       !strncmp(current_path,":dlna:",6) || !strncmp(current_path,":versions:",10)) {
        snprintf(current_path,sizeof(current_path),"%s",current_parent_path);
        return;
    }
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

/* Remote playback has no relationship to the PSP's currently browsed folder.
 * Resolve successors from the media ID on the server, only after natural EOF. */
static int remote_next_media(char *media_id, size_t capacity, int is_audio, int direction) {
    char path[ID_SIZE + 64], next_id[ID_SIZE], kind[16];
    int result;
    snprintf(path, sizeof(path), "/api/media-next/%s?shuffle=%d&direction=%s", media_id,
             is_audio && audio_shuffle, direction<0?"previous":"next");
    result = http_get(path, response, sizeof(response));
    if (result < 0) return result;
    if (!json_value(response, "id", next_id, sizeof(next_id))) return 0;
    if (!json_value(response, "kind", kind, sizeof(kind)) ||
        strcmp(kind, is_audio ? "audio" : "video") || !strcmp(media_id, next_id) ||
        strlen(next_id) >= capacity) return 0;
    /* A new Stop/Play during the transition takes precedence over autoplay.
     * Leave it for the normal command consumer; do not create commands here. */
    snprintf(path, sizeof(path), "/api/remote/next?after=%d", remote_control_sequence);
    result = http_get_wait(path, response, sizeof(response), 1000);
    if (result < 0) return result;
    if (!json_value(response, "action", kind, sizeof(kind)) || strcmp(kind, "idle")) return 0;
    strcpy(media_id, next_id);
    return 1;
}

#include "library_fetch.h"
#include "library_request.h"
static int stop_browser_requests(void) {
    library_cancel();
    int browser_stopped=browser_remote_stop();
    if(library_pending)library_request_poll();
    return browser_stopped && library_thread<0;
}

static void refresh_library(void) {
    char encoded_path[ID_SIZE * 3 + 1];
    if(library_pending)return;
    menu_art_select("");
    if (!network_ready || !http_ready) {
        strcpy(status, tr(TXT_WIFI_NOT_READY));
        return;
    }
    strcpy(status, tr(TXT_LOADING_LIBRARY));
    url_encode(current_path, encoded_path, sizeof(encoded_path));
    snprintf(library_request_path, sizeof(library_request_path), "/api/library?path=%s", encoded_path);
    library_pending=1;library_cancelled=0;library_result=-1005;library_attempt=0;
    library_started=sceKernelGetSystemTimeWide();
}

static void finish_library_request(void) {
    if (library_cancelled || library_result < 0) {
        if(library_cancelled)snprintf(status,sizeof(status),"%s",tr(TXT_LIBRARY_CANCELLED));
        else snprintf(status, sizeof(status), tr(TXT_SERVER_ERROR), library_result);
        snprintf(current_path,sizeof(current_path),"%s",library_loaded_path);
        /* A just-restored hotspot often has IP before DNS.  Keep the useful
         * browser state visible so Square can simply be tried again. */
        return;
    }
    parse_library();
    snprintf(library_loaded_path,sizeof(library_loaded_path),"%s",current_path);
    if(!current_path[0] && item_count<MAX_ITEMS) {
        memmove(items+1,items,item_count*sizeof(*items));
        memset(items,0,sizeof(*items));items[0].is_folder=2;
        snprintf(items[0].title,TITLE_SIZE,"%s",tr(TXT_LOCAL_STORAGE));item_count++;
    }
    snprintf(status, sizeof(status), tr(TXT_ENTRIES), item_count);
}

/* The library is intentionally drawn with the same inexpensive VRAM
 * primitives as the receiver strip.  It replaces the old diagnostic-console
 * landing page.  No video decoder buffers or textures are involved here. */
static void gui_library_shell(const char *section) {
    theme_text_active=1;
    u32 *vram = (u32 *)0x44000000;
    int x, lit_left = vu_left / 10, lit_right = vu_right / 10;
    int y;
    menu_skin_load();
    if (menu_skin) {
        for (y = 0; y < VIDEO_HEIGHT; y++)
            memcpy(vram + y * VIDEO_STRIDE, menu_skin + y * VIDEO_WIDTH * 4, VIDEO_WIDTH * 4);
        gui_skin_receiver(vram);
        if(!strcmp(section,tr(TXT_MEDIA_LIBRARY)) || !strcmp(section,tr(TXT_PREPARING_MEDIA)) ||
           !strcmp(section,tr(TXT_FILE_DETAILS)) || !strcmp(section,tr(TXT_STREAM_OPTIONS)))
            menu_art_draw(vram,VIDEO_STRIDE,LCD_LEFT_X,LCD_LEFT_Y,LCD_LEFT_R-LCD_LEFT_X,LCD_LEFT_B-LCD_LEFT_Y,0);
        if(!strcmp(section,tr(TXT_PREPARING_MEDIA)) || !strcmp(section,tr(TXT_STREAM_OPTIONS)))
            menu_art_draw(vram,VIDEO_STRIDE,376,110,71,LCD_RIGHT_B-110,1);
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
    music_transition_end();
    int i, first, last;
    if (selected < 0 || selected >= item_count) selected = 0;
    menu_art_select(item_count?items[selected].value:"");
    if (tv_ui_active) {
        tv_compose_view(TV_VIEW_LIBRARY, selected, 0, 0, NULL, 0);
        if(tls_notice())tv_text(34,302,48,1,TV_AMBER,"%s",tr((TextId)(TXT_TLS_FIRST+tls_notice()-1)));
        else if(!network_ready)tv_text(34,302,48,1,TV_AMBER,"%s",status);
        tv_present();return;
    }
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
    if (item_count) gui_text(376, 57, 0x00FFFFFF, "%s", items[selected].title);
    else gui_text(376, 57, 0x00FFFFFF, "%s", tr(TXT_WAITING));
    if(menu_art_has_cover()) {
        menu_art_draw((u32 *)0x44000000,VIDEO_STRIDE,376,70,72,65,1);
        if(item_count)gui_text(376,138,0x008A9BAA,"%s",items[selected].is_folder?tr(TXT_FOLDER):tr(items[selected].is_audio?TXT_MUSIC:TXT_VIDEO));
        gui_text(376,148,0x008A9BAA,tr(TXT_ENTRIES),item_count);
    } else {
    if (item_count) gui_text(376, 76, 0x008A9BAA, "%s", items[selected].is_folder ? tr(TXT_FOLDER) : (items[selected].is_audio ? tr(TXT_MUSIC) : tr(TXT_VIDEO)));
    gui_text(376, 90, 0x008A9BAA, tr(TXT_ENTRIES), item_count);
    if(debug_enabled) gui_text(376, 104, 0x008A9BAA, tr(TXT_PROFILE), active_network_profile);
    if (hardware_runtime_result == 0 && debug_enabled) gui_text(376, 118, 0x008A9BAA, "%s", tr(TXT_AVC_READY));
    else if (hardware_runtime_result != 0 && hardware_runtime_result != -9999) gui_text(376, 118, 0x008A9BAA, "%s", tr(TXT_AVC_ERROR));
    gui_text(376, 138, 0x00FFFFFF, "%s", status);
    }
    /* The tiny receiver sidebar intentionally clips ordinary status copy.
     * Decoder diagnostics need their complete signed hex code, however. */
    if (!strncmp(status, "MP3 ", 4)) gui_text(38, 160, 0x00FFB000, "%s", status);
    if(tls_notice())gui_text(38,166,0x0000D8FF,"%s",tr((TextId)(TXT_TLS_FIRST+tls_notice()-1)));
    else if(!network_ready)gui_text(38,166,0x0000D8FF,"%.48s",status);
    else if(menu_art_has_cover())gui_text(38,166,0x008A9BAA,"%.48s",status);
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
        keep_awake();
        if (tv_ui_active) tv_draw_view(TV_VIEW_INFO, selected, 0, 0, NULL, 0);
        else {
            gui_library_shell(tr(TXT_FILE_DETAILS));
            gui_text(38, 40, 0x0000D8FF, "%s", tr(TXT_FILE_DETAILS));
            gui_text(38, 57, 0x00FFFFFF, "%.39s", items[selected].title);
            gui_text(38, 80, 0x008A9BAA, "TYPE: %s", items[selected].is_audio ? tr(TXT_MUSIC_STREAM) : tr(TXT_VIDEO_STREAM));
            if (current_duration_seconds > 0.0f)
                gui_text(38, 96, 0x008A9BAA, tr(TXT_DURATION), minutes, seconds);
            else gui_text(38, 96, 0x008A9BAA, "%s", tr(TXT_DURATION_UNKNOWN));
            if(items[selected].is_audio || current_media_plex) {
                gui_text(38,112,0x00FFFFFF,"%.39s",current_media_title);
                gui_text(38,126,0x008A9BAA,"%.39s",current_media_artist);
                gui_text(38,140,0x008A9BAA,"%.39s",current_media_album);
            } else {
                gui_text(38, 112, 0x008A9BAA, tr(TXT_AUDIO_TRACKS), audio_track_count);
                gui_text(38, 128, 0x008A9BAA, tr(TXT_SUBTITLE_TRACKS), subtitle_track_count);
            }
            gui_text(376, 40, 0x00FFB000, "%s", tr(TXT_STREAMS));
            int art_top=67+(audio_track_count+subtitle_track_count)*10;
            if(art_top<132)menu_art_draw((u32 *)0x44000000,VIDEO_STRIDE,376,art_top,71,LCD_RIGHT_B-art_top,1);
            if (!audio_track_count && !subtitle_track_count) gui_text(376, 57, 0x008A9BAA, "%s", tr(TXT_NO_TRACKS));
            for (i = 0; i < audio_track_count && i < 6; i++)
                gui_text(376, 57 + i * 10, 0x00FFFFFF, "A%d %.10s", i + 1, audio_tracks[i].language);
            for (i = 0; i < subtitle_track_count && i + audio_track_count < 10; i++)
                gui_text(376, 57 + (i + audio_track_count) * 10, 0x008A9BAA, "S%d %.10s", i + 1, subtitle_tracks[i].language);
            gui_text(38, 177, 0x00FFFFFF, "%s", tr(TXT_INFO_CONTROLS));
        }
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
        keep_awake();
        if (tv_ui_active) tv_draw_view(TV_VIEW_OPTIONS, 0, row, audio_only, NULL, 0);
        else {
            gui_library_shell(tr(TXT_STREAM_OPTIONS));
            gui_text(38, 40, 0x0000D8FF, "%s", tr(TXT_STREAM_OPTIONS));
            if (audio_only) {
                if (row == 0) gui_rect((u32 *)0x44000000, 36, 64, 310, 9, 0x004A5A32);
                if (row == 1) gui_rect((u32 *)0x44000000, 36, 84, 310, 9, 0x004A5A32);
                if (row == 2) gui_rect((u32 *)0x44000000, 36, 104, 310, 9, 0x004A5A32);
                gui_text(38, 64, 0x00FFFFFF, "%s: %s", tr(TXT_QUALITY), audio_quality_name());
                if(audio_only!=2)gui_text(38, 84, 0x00FFFFFF, "%s: %s", tr(TXT_PLAY_ORDER), tr(audio_shuffle ? TXT_SHUFFLE : TXT_SEQUENTIAL));
                if(audio_only!=2)gui_text(38, 104, 0x00FFFFFF, "%s: %s",tr(TXT_PLAY_MODE),tr(download_before_play?TXT_DOWNLOAD_MODE:TXT_STREAM_MODE));
                gui_text(376, 47, 0x00FFB000, "%s", tr(TXT_QUALITY));
                gui_text(376, 76, 0x008A9BAA, "%s", tr(TXT_SAVED_FOR));
                gui_text(376, 87, 0x008A9BAA, "%s", tr(TXT_NEXT_MUSIC));
                gui_text(376, 98, 0x008A9BAA, "%s", tr(TXT_STREAM_BANG));
                gui_text(38, 177, 0x00FFFFFF, "%s", tr(TXT_MUSIC_SETUP_CONTROLS));
            } else {
                if (row == 0) gui_rect((u32 *)0x44000000, 36, 64, 310, 9, 0x004A5A32);
                if (row == 1) gui_rect((u32 *)0x44000000, 36, 84, 310, 9, 0x004A5A32);
                if (row == 2) gui_rect((u32 *)0x44000000, 36, 104, 310, 9, 0x004A5A32);
                if (row == 3) gui_rect((u32 *)0x44000000, 36, 124, 310, 9, 0x004A5A32);
                if (row == 4) gui_rect((u32 *)0x44000000, 36, 144, 310, 9, 0x004A5A32);
                gui_text(38, 144, 0x00FFFFFF, "%s: %s",tr(TXT_PLAY_MODE),tr(download_before_play?TXT_DOWNLOAD_MODE:TXT_STREAM_MODE));
                gui_text(38, 124, 0x00FFFFFF, "%s: %s", tr(TXT_FRAME_RATE), selected_video_fps ? "23.976 fps" : "20 fps");
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
        }
        sceCtrlReadBufferPositive(&pad, 1);
        if ((pad.Buttons & PSP_CTRL_CIRCLE) && !(old & PSP_CTRL_CIRCLE)) return 0;
        if ((pad.Buttons & PSP_CTRL_CROSS) && !(old & PSP_CTRL_CROSS)) return 1;
        if ((pad.Buttons & PSP_CTRL_SQUARE) && !(old & PSP_CTRL_SQUARE)) {
            help_open(audio_only?HELP_MUSIC:HELP_OPTIONS);
            sceCtrlReadBufferPositive(&pad,1);old=pad.Buttons;continue;
        }
        if (audio_only && (pad.Buttons & PSP_CTRL_UP) && !(old & PSP_CTRL_UP)) row = (row + 2) % (audio_only==2?1:3);
        if (audio_only && (pad.Buttons & PSP_CTRL_DOWN) && !(old & PSP_CTRL_DOWN)) row = (row + 1) % (audio_only==2?1:3);
        if (!audio_only && (pad.Buttons & PSP_CTRL_UP) && !(old & PSP_CTRL_UP)) row = (row + 4) % 5;
        if (!audio_only && (pad.Buttons & PSP_CTRL_DOWN) && !(old & PSP_CTRL_DOWN)) row = (row + 1) % 5;
        if ((pad.Buttons & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) && !(old & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT))) {
            int delta = (pad.Buttons & PSP_CTRL_RIGHT) ? 1 : -1;
            if (audio_only && row == 0) selected_audio_quality = (selected_audio_quality + delta + 7) % 7;
            else if (audio_only && row==1) audio_shuffle = !audio_shuffle;
            else if (audio_only) download_before_play = !download_before_play;
            else if (row == 0 && audio_track_count)
                selected_audio_track = (selected_audio_track + delta + audio_track_count) % audio_track_count;
            else if (row == 1) {
                if (subtitle_track_count) {
                    selected_subtitle_track += delta;
                    if (selected_subtitle_track < -1) selected_subtitle_track = subtitle_track_count - 1;
                    if (selected_subtitle_track >= subtitle_track_count) selected_subtitle_track = -1;
                }
            } else if (row == 2) selected_audio_quality = (selected_audio_quality + delta + 7) % 7;
            else if (row == 3) selected_video_fps = !selected_video_fps;
            else if (row == 4) download_before_play = !download_before_play;
            save_playback_settings();
        }
        old = pad.Buttons;
        sceKernelDelayThread(75000);
    }
}

#include "help_ui.h"
#include "app_settings.h"
#include "comfort_ui.h"
#include "offline_ui.h"

int main(void) {
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    SceCtrlData pad;
    unsigned int old_buttons = 0;
    unsigned int browser_deferred_buttons=0;
    unsigned long long browser_wait_tick=0;
    unsigned long long next_repeat_tick = 0;
    unsigned long long next_page_repeat_tick = 0;
    unsigned long long next_tv_redraw_tick = 0;
    int selected = 0;
    int dirty = 1;
    int result;
    setup_callbacks();
    tls_init();
    load_playback_settings();
    diagnostic_history_start();
    comfort_load_file(COMFORT_PATH,&comfort_store);
    pspDebugScreenInit();
    pspDebugScreenSetXY(0, 0);
    /* Hold L at startup to bypass an unavailable TV without editing config. */
    sceCtrlReadBufferPositive(&pad, 1);
    if (!(pad.Buttons & PSP_CTRL_LTRIGGER)) tv_ui_start();
    /* ARK-5's true overclock is managed by its own runlevel setting.  The
    * legacy systemctrl speed API tops out at the Sony 333-MHz range, so do
    * not call it here and accidentally undo a Homebrew overclock. */
    performance_result = 0;
    strcpy(status, tr(TXT_CONNECTING_WIFI));
    /* Present the media appliance immediately.  Network association can take
     * several seconds on a PSP; leaving the old blank debug screen there made
     * the application appear to start only after a file was chosen. */
    show(0);
    items[0].is_folder=2;snprintf(items[0].title,TITLE_SIZE,"%s",tr(TXT_LOCAL_STORAGE));item_count=1;
    /* Hold R to enter the local library without waiting for an access point. */
    result = wait_for_network(!(pad.Buttons & PSP_CTRL_RTRIGGER));
    /* Keep the established network/module order, but initialise AVC even if
     * association failed: local playback has no network dependency. */
    hardware_runtime_result = load_hardware_avc_runtime();
    if (result < 0) {
        if(pad.Buttons & PSP_CTRL_RTRIGGER)snprintf(status,sizeof(status),"%s",tr(TXT_LOCAL_STORAGE));
        else snprintf(status, sizeof(status), tr(TXT_NETWORK_FAILED), failure_step, result);
    } else {
        network_ready = 1;
        http_ready = 1;
        refresh_library();
    }
    while (1) {
        unsigned long long now;
        if(app_exit_requested)break;
        playback_clock_idle();
        keep_awake();
        sceCtrlReadBufferPositive(&pad, 1);
        now = sceKernelGetSystemTimeWide();
        comfort_expired(); /* An idle expiry must not stop the next manual start. */
        if(comfort_shortcut_pending && !library_pending) {
            comfort_shortcut_pending=0;
            items[0]=comfort_shortcut;item_count=1;selected=0;
            pad.Buttons=PSP_CTRL_CROSS;old_buttons=0;
        }
        /* Exit must not wait behind a stalled directory/remote request. */
        if (((pad.Buttons & PSP_CTRL_START) && !(old_buttons & PSP_CTRL_START)) ||
            (browser_deferred_buttons & PSP_CTRL_START)) break;
        if(library_pending) {
            if(pad.Buttons & PSP_CTRL_CIRCLE)library_cancel();
            int completed=library_request_poll();
            if(completed==1) {
                finish_library_request();
                if(selected>=item_count)selected=item_count?item_count-1:0;
                show(selected);
            } else if(now>=next_tv_redraw_tick) {
                if(library_cancelled || library_thread<0)
                    snprintf(status,sizeof(status),"%s %us [O]",tr(TXT_NETWORK_STOPPING),
                        (unsigned int)((now-library_started)/1000000ULL));
                else snprintf(status,sizeof(status),tr(TXT_LIBRARY_ATTEMPT),library_attempt,LIBRARY_MAX_ATTEMPTS,
                    (unsigned int)((now-library_started)/1000000ULL));
                show(selected);next_tv_redraw_tick=now+150000ULL;
            }
            old_buttons=pad.Buttons;
            sceKernelDelayThread(20000);
            continue;
        }
        if(menu_art_changed){dirty=1;menu_art_changed=0;}
        /* Stop before anything that can change network settings, use the
         * library buffer, or launch another remote worker. Navigation can
         * continue while HTTPS is connecting. Local input takes precedence. */
        unsigned int browser_action = pad.Buttons & (PSP_CTRL_START | PSP_CTRL_CIRCLE |
            PSP_CTRL_SELECT | PSP_CTRL_SQUARE | PSP_CTRL_LEFT | PSP_CTRL_TRIANGLE | PSP_CTRL_CROSS);
        pad.Buttons|=browser_deferred_buttons;
        browser_action|=browser_deferred_buttons;
        if ((!network_ready || browser_action) && !browser_remote_stop()) {
            browser_deferred_buttons=browser_action;
            if(now>=browser_wait_tick) {
                snprintf(status,sizeof(status),"%s: %s",tr(TXT_NETWORK_STOPPING),remote_http_stage);
                show(selected);browser_wait_tick=now+150000ULL;
            }
            sceKernelDelayThread(20000);
            continue;
        }
        browser_deferred_buttons=0;
        if (network_ready && !browser_action) {
            char remote_media_id[ID_SIZE];
            int remote_audio, remote_subtitle, remote_is_audio, remote_start;
            if (remote_poll_play(remote_media_id, sizeof(remote_media_id), &remote_audio,
                                 &remote_subtitle, &remote_is_audio, &remote_start)) {
                menu_art_select(remote_media_id);
                selected_audio_track = remote_audio;
                selected_subtitle_track = remote_subtitle;
                stream_start_seconds = remote_start;
                resume_pending = 0;
                seek_requested = 0;
                if((result=load_media_metadata(remote_media_id))<0) {
                    show(selected);old_buttons=pad.Buttons;continue;
                }
                snprintf(status, sizeof(status), "%s", remote_is_audio ? tr(TXT_STARTING_MUSIC) : tr(TXT_STARTING_VIDEO));
                show(selected);
                do {
                    result = remote_is_audio ? comfort_play_audio(remote_media_id, current_media_name[0]?current_media_name:"Remote stream") : comfort_play_video(remote_media_id);
                    if (result < 0) break;
                    if (resume_pending && seek_requested) {
                        /* The caller is consuming this seek now. Do not let
                         * its stale resume flag veto autoplay at the later
                         * natural EOF. A new network failure sets it again. */
                        resume_pending = seek_requested = 0;
                        sceKernelDelayThread(250000);
                        continue;
                    }
                    if (!video_file_direction && (!playback_reached_end || resume_pending)) break;
                    {
                        int following = remote_next_media(remote_media_id, sizeof(remote_media_id), remote_is_audio,
                            video_file_direction?video_file_direction:1);
                        if (following < 0) { result = following; video_step = "Next media"; break; }
                        if (!following) break;
                    }
                    stream_start_seconds = 0;
                    resume_pending = seek_requested = 0;
                    selected_audio_track = remote_audio;
                    selected_subtitle_track = remote_subtitle;
                    if((result=load_media_metadata(remote_media_id))<0)break;
                    sceKernelDelayThread(500000);
                } while (1);
                ui_restore_after_playback();
                if (result < 0) snprintf(status, sizeof(status), "%s: %08X", video_step, result);
                dirty = 1;
                old_buttons = pad.Buttons;
                continue;
            }
        }
        if ((pad.Buttons & PSP_CTRL_START) && !(old_buttons & PSP_CTRL_START)) break;
        if ((pad.Buttons & PSP_CTRL_CIRCLE) && !(old_buttons & PSP_CTRL_CIRCLE)) {
            menu_art_select("");
            offline_browser();dirty=1;old_buttons=PSP_CTRL_CIRCLE|PSP_CTRL_CROSS;continue;
        }
        if (debug_enabled && (pad.Buttons & (PSP_CTRL_SELECT | PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) ==
            (PSP_CTRL_SELECT | PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER) &&
            (old_buttons & (PSP_CTRL_SELECT | PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) !=
            (PSP_CTRL_SELECT | PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) {
            result = tvout_component_test();
            snprintf(status, sizeof(status), result == 0 ? "480p test complete" : "TV-out test: %08X", result);
            dirty = 1;
            old_buttons = pad.Buttons;
            continue;
        }
        if ((pad.Buttons & PSP_CTRL_SELECT) && !(old_buttons & PSP_CTRL_SELECT) &&
            !(pad.Buttons & (PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER))) {
            memset(&comfort_focus,0,sizeof(comfort_focus));
            if(item_count)comfort_focus=items[selected];
            if(app_settings()>0 && !comfort_shortcut_pending) {selected=0;refresh_library();}
            dirty=1;old_buttons=PSP_CTRL_SELECT|PSP_CTRL_START|PSP_CTRL_CIRCLE;continue;
        }
        if ((pad.Buttons & PSP_CTRL_SQUARE) && !(old_buttons & PSP_CTRL_SQUARE)) {
            int state=0;
            tls_notice_clear();
            /* Remote worker was joined above. Only the idle browser may
             * explicitly tear down an association; no decoder/module reset. */
            if((pad.Buttons & PSP_CTRL_LTRIGGER) || !network_ready ||
               sceNetApctlGetState(&state)<0 || state!=PSP_NET_APCTL_STATE_GOT_IP) {
                network_ready=http_ready=0;
                snprintf(status,sizeof(status),"%s",tr(TXT_CONNECTING_WIFI));show(selected);
                result=wifi_associate((pad.Buttons & PSP_CTRL_LTRIGGER)!=0);
                if(result==0) {
                    network_ready=http_ready=1;
                    have_cached_server_address=0;
                    refresh_library();
                } else snprintf(status,sizeof(status),tr(TXT_NETWORK_FAILED),failure_step,result);
            } else refresh_library();
            if(selected>=item_count)selected=item_count?item_count-1:0;
            dirty=1;old_buttons=pad.Buttons;continue;
        }
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
                int page_rows = tv_ui_active ? TV_GUI_ROWS : GUI_LIST_ROWS;
                if (trigger & PSP_CTRL_RTRIGGER) selected = (selected + page_rows) % item_count;
                else selected = (selected + item_count - (page_rows % item_count)) % item_count;
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
            if(load_media_metadata(items[selected].value)<0) {
                show(selected);old_buttons=pad.Buttons;continue;
            }
            media_info(selected);
            dirty = 1;
            old_buttons = pad.Buttons;
            continue;
        }
        if (item_count && (pad.Buttons & PSP_CTRL_CROSS) && !(old_buttons & PSP_CTRL_CROSS) && items[selected].is_folder==3) {
            snprintf(offline_requested_id,sizeof(offline_requested_id),"%.32s",items[selected].value);
            offline_browser();dirty=1;old_buttons=PSP_CTRL_CIRCLE|PSP_CTRL_CROSS;continue;
        }
        if (item_count && (pad.Buttons & PSP_CTRL_CROSS) && !(old_buttons & PSP_CTRL_CROSS) && items[selected].is_folder==2) {
            offline_browser();dirty=1;old_buttons=PSP_CTRL_CIRCLE|PSP_CTRL_CROSS;continue;
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
                if(load_media_metadata(items[selected].value)<0) {
                    show(selected);old_buttons=pad.Buttons;continue;
                }
                if (!playback_options(radio_is_live(items[selected].value)?2:items[selected].is_audio)) { dirty = 1; old_buttons = pad.Buttons; continue; }
                if(!items[selected].is_audio && !download_before_play && !comfort_resume_prompt(items[selected].value,0)) {
                    dirty=1;old_buttons=PSP_CTRL_CROSS|PSP_CTRL_CIRCLE;continue;
                }
                if(!radio_is_live(items[selected].value) && download_before_play) {
                    offline_enqueue_play(items[selected].value,items[selected].is_audio);
                    dirty=1;old_buttons=PSP_CTRL_CROSS|PSP_CTRL_CIRCLE;continue;
                }
            }
            do {
                int next;
                snprintf(status, sizeof(status), "%s", items[selected].is_audio ? tr(TXT_STARTING_MUSIC) : tr(TXT_STARTING_VIDEO));
                if(!music_transition || !items[selected].is_audio)show(selected);
                result = items[selected].is_audio ? comfort_play_audio(items[selected].value, items[selected].title) : comfort_play_video(items[selected].value);
                ui_restore_after_playback();
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
                if((item_count==1 || !strncmp(items[selected].value,"plex.",5) || !strncmp(items[selected].value,"jellyfin.",9) || !strncmp(items[selected].value,"dlna.",5)) && (playback_reached_end || video_file_direction)) {
                    char following_id[ID_SIZE];
                    snprintf(following_id,sizeof(following_id),"%s",items[selected].value);
                    int following=remote_next_media(following_id,sizeof(following_id),items[selected].is_audio,video_file_direction);
                    if(following>0) {
                        snprintf(items[selected].value,sizeof(items[selected].value),"%s",following_id);
                        stream_start_seconds=0;
                        if(load_media_metadata(following_id)<0)break;
                        snprintf(items[selected].title,sizeof(items[selected].title),"%s",current_media_name);
                        continue;
                    }
                    break;
                }
                next = (playback_reached_end || video_file_direction>0) ? next_media_index(selected, items[selected].is_audio) : -1;
                if(video_file_direction<0) {
                    for(next=selected-1;next>=0;next--)
                        if(!items[next].is_folder && items[next].is_audio==items[selected].is_audio) break;
                }
                if (next < 0) {
                    if(!debug_enabled)
                        snprintf(status,sizeof(status),"%s",tr(items[selected].is_audio?TXT_MUSIC_FINISHED:TXT_VIDEO_FINISHED));
                    else if (items[selected].is_audio)
                        snprintf(status, sizeof(status), tr(TXT_MUSIC_ENDED), audio_state);
                    else
                        snprintf(status, sizeof(status), tr(TXT_VIDEO_ENDED), result);
                    break;
                }
                selected = next;
                resume_pending = 0;
                stream_start_seconds = 0;
                snprintf(status, sizeof(status), items[selected].is_audio ? tr(TXT_NEXT_TRACK) : tr(TXT_NEXT_EPISODE), items[selected].title);
                if(!music_transition)show(selected);
                if(load_media_metadata(items[selected].value)<0)break;
            } while (1);
            if(!strncmp(items[selected].value,"plex.",5) || !strncmp(items[selected].value,"jellyfin.",9)) {
                refresh_library();
                if(selected>=item_count)selected=item_count?item_count-1:0;
            }
            dirty = 1;
        }
        old_buttons = pad.Buttons;
        if (dirty) { show(selected); dirty = 0;next_tv_redraw_tick=now+150000ULL; }
        /* Idle animations update only their strip, and pause during artwork
         * downloads. Real input/status changes still redraw immediately. */
        else if(tv_ui_active && !browser_art_busy() && now>=next_tv_redraw_tick) {
            tv_library_receiver_refresh();next_tv_redraw_tick=now+150000ULL;
        }
        sceKernelDelayThread(20000);
    }
    int browser_stopped=exit_join_worker(stop_browser_requests);
    music_transition_end();
    if(browser_stopped){free(menu_art_active);menu_art_active=NULL;}
    prepare_oc_exit();
    if (display_output.tv) display_output_select(&display_output, 0);
    free(tv_canvas.pixels);
    /* A cancelled DNS/TLS worker can still be unwinding. Never pull its
     * network modules away. loadexec owns final cleanup on timeout. */
    if(browser_stopped) {
        input_remote_stop();
        sceNetApctlTerm();
        sceNetInetTerm();
        sceNetTerm();
        sceUtilityUnloadNetModule(PSP_NET_MODULE_INET);
        sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
    }
    sceKernelExitGame();
    return 0;
}
