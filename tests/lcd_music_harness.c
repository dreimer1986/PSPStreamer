#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/mman.h>
#include "language.h"
typedef uint32_t u32;
#define VIDEO_WIDTH 480
#define VIDEO_HEIGHT 272
#define VIDEO_STRIDE 512
#define MENU_SKIN_BYTES (VIDEO_WIDTH * VIDEO_HEIGHT * 4)
#define FRAME_BYTES (VIDEO_STRIDE * VIDEO_HEIGHT * 4)
#define SUBTITLE_FONT_CELL_WIDTH 16
#define SUBTITLE_FONT_CELL_HEIGHT 20
#define SPECTRUM_BANDS 12
#define PSP_CTRL_LTRIGGER 1
#define PSP_CTRL_SELECT 2
#define PSP_CTRL_RTRIGGER 4
#define PSP_DISPLAY_PIXEL_FORMAT_8888 3
#define PSP_DISPLAY_SETBUF_NEXTVSYNC 1
static int tv_ui_active, tvout_video_active;
static struct { int tv; } display_output;
static int audio_running = 1, audio_start = 1, playback_volume = 24;
static int vu_left, vu_right, vu_display_left, vu_display_right;
static int spectrum_levels[12], spectrum_display[12];
static unsigned int receiver_flash_button;
static unsigned long long clock_tick;
static int framebuffer_calls;
extern const unsigned char receiver_skin[], receiver_skin_end[];
static const unsigned char *menu_skin;
static unsigned char font[81920], *subtitle_font = font;
static void subtitle_load_font(void) {}
static int subtitle_utf8_char(const char **text) {
    const unsigned char *s = (const unsigned char *)*text;
    if (s[0] == 0xc3 && s[1]) { *text += 2; return 0xc0 + (s[1] & 63); }
    if (s[0] == 0xc2 && s[1]) { *text += 2; return s[1]; }
    (*text)++; return s[0] < 128 ? s[0] : '?';
}
static unsigned long long sceKernelGetSystemTimeWide(void) { return clock_tick; }
static void sceDisplayWaitVblankStart(void) {}
static int sceDisplaySetFrameBuf(void *address, int stride, int format, int sync) {
    assert(address == (void *)0x04000000 && stride == 512 && format == 3 && sync == 1);
    assert(!display_output.tv && !tvout_video_active);
    framebuffer_calls++; return 0;
}
static void receiver_hud(int frames) { (void)frames; }
__asm__(".section .rodata\n.global receiver_skin\n.global receiver_skin_end\n"
        "receiver_skin:\n.incbin \"assets/menu_skin.raw\"\nreceiver_skin_end:\n.text\n");
/* PRODUCTION_FUNCTIONS */
static int sceKernelGetThreadCurrentPriority(void) { return 0x20; }
static int sceKernelChangeThreadPriority(int thread, int priority) {
    (void)thread; (void)priority; return 0;
}
#include "music_ui.h"
#include "lcd_music.h"

int main(void) {
    /* Independent legacy formula, so sharing the new helper between full
     * and partial renderers cannot hide an envelope regression. */
    for (int value = 0; value <= 100; value++)
        for (int target = 0; target <= 100; target++) {
            int expected = value;
            if (target > expected) expected += (target - expected + 1) / 2;
            else if (expected > 3) expected -= 3;
            else expected = 0;
            assert(music_ui_envelope(value, target) == expected);
        }
    u32 *vram = (u32 *)0x44000000;
    u32 *incremental = malloc(FRAME_BYTES);
    FILE *font_file = fopen("assets/subtitle_font.raw", "rb");
    assert(font_file && fread(font, 1, sizeof(font), font_file) == sizeof(font));
    fclose(font_file);
    assert(incremental);
    assert(mmap(vram, FRAME_BYTES, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == vram);
    for (int i = 0; i < VIDEO_STRIDE * VIDEO_HEIGHT; i++) vram[i] = 0xdeadbeef;
    for (int lang = 0; lang < 2; lang++) {
        language_set_code(lang ? "de" : "en");
        for (int fullscreen = 0; fullscreen < 2; fullscreen++) {
            unsigned long long max_bytes = 0;
            audio_running = 1;
            lcd_music_reset();
            lcd_draw_music("Music / Grüße aus München", fullscreen);
            assert(lcd_music.full_frames == 1);
            lcd_draw_music("Music / Grüße aus München", fullscreen);
            assert(lcd_music.incremental_frames == 0);
            for (int frame = 0; frame < 180; frame++) {
                int spectrum_before[12], left_before, right_before, calls;
                unsigned long long before = lcd_music.restored_bytes, bytes;
                clock_tick += 60000;
                playback_volume = frame < 93 ? frame % 31 : 15;
                vu_left = frame * 7 % 101; vu_right = frame * 19 % 101;
                receiver_flash_button = frame % 8;
                audio_start = frame < 90 || frame > 125;
                audio_running = frame < 160;
                for (int i = 0; i < 12; i++)
                    spectrum_levels[i] = frame < 40 ? 100 : (frame * 17 + i * 31) % 101;
                memcpy(spectrum_before, spectrum_display, sizeof(spectrum_before));
                left_before = vu_display_left; right_before = vu_display_right;
                calls = framebuffer_calls;
                lcd_draw_music("Music / Grüße aus München", fullscreen);
                assert(framebuffer_calls == calls);
                bytes = lcd_music.restored_bytes - before;
                if (bytes > max_bytes) max_bytes = bytes;
                assert(bytes < MENU_SKIN_BYTES / 8);
                memcpy(incremental, vram, FRAME_BYTES);
                memcpy(spectrum_display, spectrum_before, sizeof(spectrum_before));
                vu_display_left = left_before; vu_display_right = right_before;
                lcd_music_full("Music / Grüße aus München", fullscreen);
                for (int y = 0; y < VIDEO_HEIGHT; y++) {
                    for (int x = 0; x < VIDEO_WIDTH; x++) {
                        if (incremental[y * VIDEO_STRIDE + x] != vram[y * VIDEO_STRIDE + x]) {
                            fprintf(stderr, "Mismatch lang=%d fullscreen=%d frame=%d x=%d y=%d\n",
                                    lang, fullscreen, frame, x, y);
                            abort();
                        }
                    }
                    for (int x = VIDEO_WIDTH; x < VIDEO_STRIDE; x++)
                        assert(vram[y * VIDEO_STRIDE + x] == 0xdeadbeef);
                }
            }
            assert(lcd_music.full_frames == 1 && lcd_music.incremental_frames == 180);
            printf("LCD music %s fullscreen=%d: average %llu, max %llu restored bytes (full=%d)\n",
                   language_code(), fullscreen, lcd_music.restored_bytes / 180, max_bytes, MENU_SKIN_BYTES);
            tv_ui_active = 1;
            lcd_draw_music("Blocked TV GUI", !fullscreen);
            tv_ui_active = 0; display_output.tv = 1;
            lcd_draw_music("Blocked TV scanout", !fullscreen);
            display_output.tv = 0; tvout_video_active = 1;
            lcd_draw_music("Blocked video owner", !fullscreen);
            tvout_video_active = 0;
            assert(lcd_music.full_frames == 1);
            lcd_draw_music("New layout", !fullscreen);
            assert(lcd_music.full_frames == 2);
            lcd_music_reset();
            lcd_draw_music("Next track", fullscreen);
            assert(lcd_music.full_frames == 1 && lcd_music.incremental_frames == 0);
        }
    }
    free(incremental); munmap(vram, FRAME_BYTES);
    return 0;
}
