/* Run the actual native TV renderer with guarded RAM and fake scanout.
 * mmap is only for reproducing this process's PSP VRAM address on the host. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <malloc.h>
#include <sys/mman.h>
#include "display_output.h"
#include "tv_canvas.h"
#include "language.h"
typedef uint32_t u32;
typedef struct { unsigned int Buttons; } SceCtrlData;
#define PSP_CTRL_LTRIGGER 1
#define PSP_CTRL_SELECT 2
#define PSP_CTRL_RTRIGGER 4
#define PSP_CTRL_CROSS 8
#define PSP_CTRL_TRIANGLE 16
#define PSP_CTRL_START 32
#define PSP_DISPLAY_PIXEL_FORMAT_8888 3
#define PSP_DISPLAY_SETBUF_NEXTVSYNC 1
#define SPECTRUM_BANDS 12
static TvCanvas tv_canvas;
static int tv_ui_auto, tv_ui_active, tvout_video_active, cable = 2;
static int mode_calls, framebuffer_calls, mode_failure, buffer_failure, hardware_tv;
static int debug_calls, load_failure;
static unsigned long long clock_tick = 1800000;
static unsigned int test_buttons;
static int ui_priority = 0x20, priority_failure;
static int sceKernelGetThreadCurrentPriority(void) { return ui_priority; }
static int sceKernelChangeThreadPriority(int thread, int priority) {
    assert(thread == 0);
    if (priority_failure) return -1;
    ui_priority = priority; return 0;
}
static unsigned char font[81920], *subtitle_font = font;
static int playback_volume = 24, vu_display_left = 30, vu_display_right = 80;
static int item_count = 40, audio_track_count = 2, subtitle_track_count = 2;
static int selected_audio_track, selected_subtitle_track, audio_shuffle;
static int audio_running = 1, audio_start = 1;
static float current_duration_seconds = 1442.0f;
static int spectrum_levels[12], spectrum_display[12];
static char current_path[512] = "/Series/Fullmetal Alchemist Brotherhood/Season 1";
static char status[160] = "MP3 start: 807F00F0";
static struct { char title[128]; int is_folder, is_audio; } items[64];
static struct { char language[16], title[48]; } audio_tracks[8], subtitle_tracks[8];
static int fake_mode(int tv) { mode_calls++; if (mode_failure) { mode_failure = 0; return -10; } hardware_tv = tv; return 0; }
static int fake_buffer(int tv) { framebuffer_calls++; assert(hardware_tv == tv); if (buffer_failure) { buffer_failure = 0; return -11; } return 0; }
static DisplayOutput display_output = {0, fake_mode, fake_buffer};
static int tvout_load_manager(void) { return load_failure ? -1 : 0; }
static int pspDveMgrCheckVideoOut(void) { return cable; }
static void pspDebugScreenInit(void) { assert(!hardware_tv); debug_calls++; }
static void subtitle_load_font(void) { subtitle_font = font; }
static int subtitle_utf8_char(const char **text) {
    const unsigned char *s = (const unsigned char *)*text;
    if (s[0] == 0xc3 && s[1]) { *text += 2; return 0xc0 + (s[1] & 63); }
    if (s[0] == 0xc2 && s[1]) { *text += 2; return s[1]; }
    (*text)++; return s[0] < 128 ? s[0] : '?';
}
static void vu_ballistics_step(void) {}
static unsigned long long sceKernelGetSystemTimeWide(void) { return clock_tick; }
static void sceCtrlPeekBufferPositive(SceCtrlData *pad, int count) { assert(count == 1); pad->Buttons = test_buttons; }
static void sceDisplayWaitVblankStart(void) {}
static int sceDisplaySetFrameBuf(void *address, int stride, int format, int sync) {
    assert(address == (void *)0x04000000 && stride == 768 && format == 3 && sync == 1);
    assert(hardware_tv && !tvout_video_active); framebuffer_calls++; return 0;
}
static const char *audio_quality_name(void) { return "160k"; }
/* Build in psp-client, so the same production incbin asset is tested. */
__asm__(".section .rodata\n.global receiver_tv_skin\n.global receiver_tv_skin_end\n"
        "receiver_tv_skin:\n.incbin \"assets/menu_skin_tv.raw\"\nreceiver_tv_skin_end:\n.text\n");
#include "music_ui.h"
#include "tv_gui.h"

static void dump_frame(const char *directory, const char *language, int view, int variant) {
    char filename[1024]; int x, y; FILE *output;
    snprintf(filename, sizeof(filename), "%s/tv-%s-%d-%d.ppm", directory, language, view, variant);
    output = fopen(filename, "wb"); assert(output);
    fprintf(output, "P6\n720 480\n255\n");
    for (y = 0; y < 480; y++) for (x = 0; x < 720; x++) {
        u32 pixel = tv_canvas.pixels[y * 768 + x];
        fputc(pixel & 255, output); fputc((pixel >> 8) & 255, output); fputc((pixel >> 16) & 255, output);
    }
    fclose(output);
}
int main(int argc, char **argv) {
    int i, language, view, variant, before;
    FILE *input = fopen("assets/subtitle_font.raw", "rb");
    assert(input && fread(font, 1, sizeof(font), input) == sizeof(font)); fclose(input);
    assert(mmap((void *)0x44000000, TV_GUI_BYTES, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == (void *)0x44000000);
    /* off/no cable/composite/module error all preserve LCD and allocate nothing. */
    tv_ui_start(); assert(!tv_ui_active && !tv_canvas.pixels && mode_calls == 0);
    i = music_ui_lower_priority(); assert(i == 0x20 && ui_priority == 0x40);
    music_ui_restore_priority(i); assert(ui_priority == 0x20);
    tv_ui_auto = 1; cable = 0; tv_ui_start(); assert(!tv_ui_active && !tv_canvas.pixels);
    cable = 1; tv_ui_start(); assert(!tv_ui_active && !tv_canvas.pixels);
    cable = 2; load_failure = 1; tv_ui_start(); assert(!tv_ui_active && !tv_canvas.pixels);
    load_failure = 0; tv_ui_start(); assert(tv_ui_active && display_output.tv && mode_calls == 1);
    i = music_ui_lower_priority(); assert(i == 0x20 && ui_priority == 0x40);
    music_ui_restore_priority(i); assert(ui_priority == 0x20);
    priority_failure = 1;
    i = music_ui_lower_priority(); assert(i == -1 && ui_priority == 0x20);
    priority_failure = 0;
    music_ui_restore_priority(i); assert(ui_priority == 0x20);
    ui_priority = 0x50;
    assert(music_ui_lower_priority() == -1 && ui_priority == 0x50);
    ui_priority = 0x20;
    before = mode_calls;
    for (i = 0; i < 100; i++) {
        /* TV menu -> video -> stop/seek/EOF -> TV menu: no mode resets. */
        assert(display_output_select(&display_output, 1) == 0);
        tvout_video_active = 1;
        int calls = framebuffer_calls;
        tv_draw_view(TV_VIEW_LIBRARY, 0, 0, 0, NULL, 0);
        assert(calls == framebuffer_calls); /* GUI cannot steal video scanout. */
        tvout_video_active = 0;
        assert(display_output_select(&display_output, tv_ui_active) == 0);
        ui_restore_after_playback(); assert(debug_calls == 0);
    }
    assert(mode_calls == before);
    display_output_select(&display_output, 0); tv_ui_active = 0;
    ui_restore_after_playback(); assert(debug_calls == 1);
    mode_failure = 1; assert(display_output_select(&display_output, 1) == -10);
    assert(!display_output.tv && !hardware_tv);
    buffer_failure = 1; assert(display_output_select(&display_output, 1) == -11);
    assert(!display_output.tv && !hardware_tv);
    display_output_select(&display_output, 1); tv_ui_active = 1;

    for (i = 0; i < item_count; i++) snprintf(items[i].title, sizeof(items[i].title), "Episode %02d - Grüße aus München.mkv", i + 1);
    for (i = 0; i < 2; i++) {
        strcpy(audio_tracks[i].language, i ? "jpn" : "deu");
        strcpy(subtitle_tracks[i].language, i ? "eng" : "deu");
        strcpy(audio_tracks[i].title, "Deutsch / Originalton");
        strcpy(subtitle_tracks[i].title, "Vollständig – äöü ß");
    }
    for (i = 0; i < 12; i++) spectrum_levels[i] = (i * 31 + 20) % 100;
    /* Sentinel padding and boundary primitives catch pitch/clip mistakes. */
    for (i = 0; i < 480; i++) for (int x = 720; x < 768; x++) tv_canvas.pixels[i * 768 + x] = 0x12345678;
    tv_rect(&tv_canvas, -5, -8, 12, 20, 1);
    tv_rect(&tv_canvas, 710, 470, 100, 100, 1);
    tv_line(&tv_canvas, -4, -4, 725, 485, 1);
    tv_glyph(&tv_canvas, font, 255, 719, 479, 1);
    for (language = 0; language < 2; language++) {
        language_set_code(language ? "de" : "en");
        for (view = 0; view <= TV_VIEW_MUSIC; view++) for (variant = 0; variant < 3; variant++) {
            tv_draw_view(view, 13, variant, variant == 1, "Music / Grüße aus München", variant == 2);
            for (i = 0; i < 480; i++) for (int x = 720; x < 768; x++)
                assert(tv_canvas.pixels[i * 768 + x] == 0x12345678);
            if (argc > 1) dump_frame(argv[1], language_code(), view, variant);
        }
    }
    /* Compare every incremental music frame to the production full renderer.
     * Include rising/falling bars, silence, all volume detents, both VUs,
     * changing/pressed indicators and both fullscreen layouts. */
    for (int fullscreen = 0; fullscreen < 2; fullscreen++) {
        u32 *incremental = malloc(TV_GUI_BYTES);
        unsigned long long start_bytes, max_bytes = 0;
        assert(incremental);
        tv_music_reset();
        tv_draw_music("Music / Grüße aus München", fullscreen);
        assert(tv_music.full_frames == 1);
        start_bytes = tv_music.copied_bytes;
        tv_draw_music("Music / Grüße aus München", fullscreen);
        assert(tv_music.incremental_frames == 0); /* budget guard, no redundant draw */
        for (int frame = 0; frame < 90; frame++) {
            int previous_spectrum[12];
            unsigned long long bytes = tv_music.copied_bytes;
            clock_tick += 60000;
            playback_volume = frame % 31;
            vu_display_left = frame * 7 % 101;
            vu_display_right = frame * 19 % 101;
            test_buttons = frame % 64;
            audio_start = frame < 50 || frame > 65;
            for (i = 0; i < 12; i++) spectrum_levels[i] = (frame * 17 + i * 31) % 101;
            memcpy(previous_spectrum, spectrum_display, sizeof(previous_spectrum));
            tv_draw_music("Music / Grüße aus München", fullscreen);
            assert(!memcmp(tv_canvas.pixels, (void *)0x44000000, TV_GUI_BYTES));
            bytes = tv_music.copied_bytes - bytes;
            if (bytes > max_bytes) max_bytes = bytes;
            assert(bytes < TV_GUI_BYTES / 4); /* no recurring full-frame copies */
            memcpy(incremental, tv_canvas.pixels, TV_GUI_BYTES);
            memcpy(spectrum_display, previous_spectrum, sizeof(previous_spectrum));
            tv_draw_view(TV_VIEW_MUSIC, 0, 0, 1, "Music / Grüße aus München", fullscreen);
            assert(!memcmp(incremental, tv_canvas.pixels, TV_GUI_BYTES));
        }
        assert(tv_music.full_frames == 1 && tv_music.incremental_frames == 90);
        printf("TV music fullscreen=%d: average %llu, maximum %llu bytes/update (full=%d)\n",
               fullscreen, (tv_music.copied_bytes - start_bytes) / 90, max_bytes, TV_GUI_BYTES);
        tvout_video_active = 1;
        tv_draw_music("Music / Grüße aus München", !fullscreen);
        assert(tv_music.full_frames == 1); /* no takeover while video owns scanout */
        tvout_video_active = 0;
        tv_draw_music("Music / Grüße aus München", !fullscreen);
        assert(tv_music.full_frames == 2); /* explicit layout change repaints once */
        free(incremental);
    }
    free(tv_canvas.pixels); munmap((void *)0x44000000, TV_GUI_BYTES);
    return 0;
}
