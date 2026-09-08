#ifndef PSPSTREAMER_LCD_MUSIC_H
#define PSPSTREAMER_LCD_MUSIC_H

/* Music-only LCD updates. No extra framebuffer and no changes to AVC scanout. */
static struct {
    int valid, fullscreen, volume, height[SPECTRUM_BANDS];
    unsigned long long next_tick, restored_bytes;
    unsigned int full_frames, incremental_frames;
} lcd_music;

static void lcd_music_reset(void) { memset(&lcd_music, 0, sizeof(lcd_music)); }

/* Retained full renderer is also the pixel-equivalence reference in tests. */
static void lcd_music_full(const char *title, int fullscreen) {
    int x;
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
        spectrum_display[x] = music_ui_envelope(spectrum_display[x], target);
        height = spectrum_display[x] * (fullscreen ? 145 : 82) / 100;
        gui_rect((u32 *)0x44000000, fullscreen ? 24 + x * 36 : 42 + x * 23, baseline - height,
                 fullscreen ? 25 : 15, height, color);
    }
    if (fullscreen) gui_audio_fullscreen_receiver((u32 *)0x44000000);
    else gui_text(38, 177, 0x00FFFFFF, "%s", tr(TXT_MUSIC_CONTROLS));
    sceDisplaySetFrameBuf((void *)0x04000000, VIDEO_STRIDE, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTVSYNC);
    sceDisplayWaitVblankStart();
}

static void lcd_music_restore(int left, int top, int width, int height, int fullscreen) {
    u32 *vram = (u32 *)0x44000000;
    int y;
    for (y = top; y < top + height; y++) {
        if (fullscreen && y < 198)
            gui_rect(vram, left, y, width, 1, 0x00080E14);
        else
            memcpy(vram + y * VIDEO_STRIDE + left,
                   menu_skin + (y * VIDEO_WIDTH + left) * 4, width * 4);
    }
    lcd_music.restored_bytes += (unsigned int)(width * height * 4);
}

static void lcd_draw_music(const char *title, int fullscreen) {
    u32 *vram = (u32 *)0x44000000;
    int i, baseline = fullscreen ? 194 : 150;
    int volume_changed, label_dirty;
    unsigned long long now = sceKernelGetSystemTimeWide();
    /* Never write an LCD-pitch frame into a TV/video-owned framebuffer. */
    if (tv_ui_active || display_output.tv || tvout_video_active) return;
    if (lcd_music.valid && lcd_music.fullscreen == fullscreen && now < lcd_music.next_tick) return;
    if (!lcd_music.valid || lcd_music.fullscreen != fullscreen || !menu_skin) {
        lcd_music_full(title, fullscreen);
        lcd_music.valid = 1;
        lcd_music.fullscreen = fullscreen;
        lcd_music.volume = playback_volume;
        for (i = 0; i < SPECTRUM_BANDS; i++)
            lcd_music.height[i] = spectrum_display[i] * (fullscreen ? 145 : 82) / 100;
        lcd_music.full_frames++;
        lcd_music.next_tick = sceKernelGetSystemTimeWide() + MUSIC_UI_INTERVAL_US;
        return;
    }
    sceDisplayWaitVblankStart();
    /* Restore only the two needle envelopes, not the whole receiver strip. */
    lcd_music_restore(43, 214, 51, 25, fullscreen);
    lcd_music_restore(137, 214, 51, 25, fullscreen);
    volume_changed = lcd_music.volume != playback_volume;
    label_dirty = volume_changed;
    if (volume_changed) {
        lcd_music_restore(393, 188, 55, 55, fullscreen);
        if (!fullscreen) lcd_music_restore(38, 64, 436, 8, 0);
        lcd_music.volume = playback_volume;
    }
    for (i = 0; i < SPECTRUM_BANDS; i++) {
        int target = (!audio_running || !audio_start) ? 0 : spectrum_levels[i];
        int previous = lcd_music.height[i], height;
        int x = fullscreen ? 24 + i * 36 : 42 + i * 23, width = fullscreen ? 25 : 15;
        u32 color = i < 4 ? 0x0000D8FF : i < 8 ? 0x00B070FF : 0x00FFB000;
        spectrum_display[i] = music_ui_envelope(spectrum_display[i], target);
        height = spectrum_display[i] * (fullscreen ? 145 : 82) / 100;
        if (height > previous)
            gui_rect(vram, x, baseline - height, width, height - previous, color);
        else if (height < previous) {
            lcd_music_restore(x, baseline - previous, width, previous - height, fullscreen);
            if (!fullscreen && baseline - previous < 72) label_dirty = 1;
        }
        lcd_music.height[i] = height;
    }
    /* At maximum height normal bars overlap the last four rows of the
     * volume label. Preserve the full renderer's text-then-bars ordering.
     * In fullscreen the knob overlaps the last six rows of the last bars. */
    if (!fullscreen && label_dirty)
        gui_text(38, 64, 0x008A9BAA, tr(TXT_VOLUME_LINE), playback_volume * 100 / 30);
    for (i = 0; i < SPECTRUM_BANDS && (fullscreen ? volume_changed : label_dirty); i++) {
        int top = baseline - lcd_music.height[i], bottom = fullscreen ? 194 : 72;
        int x = fullscreen ? 24 + i * 36 : 42 + i * 23, width = fullscreen ? 25 : 15;
        u32 color = i < 4 ? 0x0000D8FF : i < 8 ? 0x00B070FF : 0x00FFB000;
        if (fullscreen && top < 188) top = 188;
        if (top < bottom) gui_rect(vram, x, top, width, bottom - top, color);
    }
    gui_skin_receiver(vram);
    /* Address/stride remain fixed. No display reconfiguration per tick. */
    lcd_music.incremental_frames++;
    lcd_music.next_tick = sceKernelGetSystemTimeWide() + MUSIC_UI_INTERVAL_US;
}
#endif
