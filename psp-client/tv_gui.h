/* Separate native TV layout; the LCD renderer is deliberately not reused.
 * Included by main.c after the audio meter/metadata helpers. */
#ifndef PSP_STREAMER_TV_GUI_H
#define PSP_STREAMER_TV_GUI_H
extern unsigned char receiver_tv_skin[], receiver_tv_skin_end[];
enum { TV_VIEW_LIBRARY, TV_VIEW_LOADING, TV_VIEW_INFO, TV_VIEW_OPTIONS, TV_VIEW_MUSIC };
#define TV_WHITE 0x00E8E8E8
#define TV_MUTED 0x00AEADA0
#define TV_CYAN  0x00EAD080
#define TV_AMBER 0x003CC9FF

static void tv_ui_start(void) {
    if (!tv_ui_auto || tvout_load_manager() < 0 || pspDveMgrCheckVideoOut() != 2) return;
    if (!tv_canvas.pixels) tv_canvas.pixels = memalign(64, TV_GUI_BYTES);
    if (!tv_canvas.pixels) return; /* Existing LCD/video-only mode is the fallback. */
    memset(tv_canvas.pixels, 0, TV_GUI_BYTES);
    memset((void *)0x44000000, 0, TV_GUI_BYTES);
    if (display_output_select(&display_output, 1) < 0) return;
    tv_ui_active = 1;
}

static void ui_restore_after_playback(void) {
    /* Only the historical LCD path may initialise the SDK debug console.
     * In native TV mode the menu will redraw after playback workers joined. */
    if (!tv_ui_active) pspDebugScreenInit();
}

static int tv_utf8_char(const char **text) {
    const unsigned char *s = (const unsigned char *)*text;
    int remaining;
    if (s[0] < 0xc4) return subtitle_utf8_char(text);
    if (s[0] == 0xe2 && s[1] == 0x80 && (s[2] == 0x93 || s[2] == 0x94)) {
        *text += 3; return '-';
    }
    remaining = s[0] < 0xe0 ? 1 : s[0] < 0xf0 ? 2 : 3;
    (*text)++;
    while (remaining-- && ((unsigned char)**text & 0xc0) == 0x80) (*text)++;
    return '?';
}

/* Proportional native glyphs with word wrap; limits remain pixel-based.
 * UTF-8 is decoded before measuring, so umlauts cannot be split into bytes. */
static void tv_text(int x, int y, int columns, int lines, u32 color, const char *format, ...) {
    char text[512];
    const char *cursor;
    static unsigned char advance[256];
    int row = 0, offset = 0, new_word = 1, limit = columns * 13;
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (!subtitle_font) subtitle_load_font();
    if (!subtitle_font) return;
    if (!advance[0]) {
        int g, xx, yy;
        for (g = 0; g < 256; g++) {
            const unsigned char *bitmap = subtitle_font + (g >> 4) * 20 * 256 + (g & 15) * 16;
            advance[g] = 6;
            for (yy = 0; yy < 16; yy++) for (xx = 0; xx < 12; xx++)
                if (bitmap[yy * 256 + xx] && advance[g] < xx + 2) advance[g] = xx + 2;
        }
    }
    cursor = text;
    while (*cursor && row < lines) {
        int glyph;
        if (new_word && *cursor != ' ' && *cursor != '\n') {
            const char *word = cursor;
            int width = 0;
            while (*word && *word != ' ' && *word != '\n') width += advance[tv_utf8_char(&word)];
            if (offset && offset + width > limit) { offset = 0; row++; }
        }
        glyph = tv_utf8_char(&cursor);
        new_word = glyph == ' ' || glyph == '\n';
        if (glyph == '\n') { offset = 0; row++; continue; }
        if (offset + advance[glyph] > limit) { offset = 0; row++; }
        if (row >= lines) break;
        if (glyph == ' ' && !offset) continue;
        tv_glyph(&tv_canvas, subtitle_font, glyph, x + offset, y + row * 16, color);
        offset += advance[glyph];
    }
}

static u32 tv_indicator_color(int index) {
    SceCtrlData pad;
    static const unsigned int masks[] = {PSP_CTRL_LTRIGGER, PSP_CTRL_SELECT,
        PSP_CTRL_RTRIGGER, PSP_CTRL_CROSS | PSP_CTRL_TRIANGLE, PSP_CTRL_START};
    static const u32 colors[] = {TV_CYAN, TV_AMBER, 0x00B070FF, 0x0060DD80, 0x00FF9060};
    unsigned int tick = (unsigned int)(sceKernelGetSystemTimeWide() / 600000ULL);
    sceCtrlPeekBufferPositive(&pad, 1);
    return pad.Buttons & masks[index] ? TV_WHITE : colors[(tick + index * 3) % 5];
}

/* Bits 0/1: meters, 2..6: indicator lights, 7: volume marker and label. */
static void tv_receiver_parts(unsigned int parts) {
    static const signed char needle_x[] = {-42,-40,-37,-34,-30,-25,-20,-15,-10,-5,0,5,10,15,20,25,30,34,37,40,42};
    static const signed char needle_y[] = {-11,-16,-21,-25,-29,-32,-35,-37,-39,-40,-40,-40,-39,-37,-35,-32,-29,-25,-21,-16,-11};
    /* Clockwise travel over the upper 270 degrees of the volume dial. */
    static const signed char knob_x[] = {-21,-24,-27,-29,-30,-30,-30,-29,-27,-24,-21,-18,-14,-9,-5,0,5,9,14,18,21,24,27,29,30,30,30,29,27,24,21};
    static const signed char knob_y[] = {21,18,14,9,5,0,-5,-9,-14,-18,-21,-24,-27,-29,-30,-30,-30,-29,-27,-24,-21,-18,-14,-9,-5,0,5,9,14,18,21};
    int i, volume = playback_volume < 0 ? 0 : playback_volume > 30 ? 30 : playback_volume;
    for (i = 0; i < 2; i++) {
        int cx = i ? 237 : 90;
        int value = ((i ? vu_display_right : vu_display_left) * 20 + 50) / 100;
        if (!(parts & (1U << i))) continue;
        if (value < 0) value = 0;
        if (value > 20) value = 20;
        tv_line(&tv_canvas, cx, 430, cx + needle_x[value], 430 + needle_y[value], TV_AMBER);
        tv_rect(&tv_canvas, cx - 2, 428, 5, 4, TV_AMBER);
    }
    for (i = 0; i < 5; i++) {
        if (parts & (1U << (i + 2)))
            tv_rect(&tv_canvas, 331 + i * 47, 387, 25, 2, tv_indicator_color(i));
    }
    if (parts & 128) {
        tv_rect(&tv_canvas, 633 + knob_x[volume] * 27 / 32 - 2, 408 + knob_y[volume] - 1, 5, 3, TV_AMBER);
        tv_rect(&tv_canvas, 633 + knob_x[volume] * 27 / 32 - 1, 408 + knob_y[volume] - 2, 3, 5, TV_AMBER);
        tv_text(607, 452, 6, 1, TV_MUTED, "%3d %%", volume * 100 / 30);
    }
}

static void tv_receiver(void) {
    vu_ballistics_step();
    tv_receiver_parts(255);
}

static void tv_shell(const char *section) {
    int y;
    if (receiver_tv_skin_end - receiver_tv_skin == TV_GUI_WIDTH * TV_GUI_HEIGHT * 4) {
        for (y = 0; y < TV_GUI_HEIGHT; y++)
            memcpy(tv_canvas.pixels + y * TV_GUI_STRIDE,
                   receiver_tv_skin + y * TV_GUI_WIDTH * 4, TV_GUI_WIDTH * 4);
    } else memset(tv_canvas.pixels, 0, TV_GUI_BYTES);
    tv_text(31, 27, 50, 1, TV_WHITE, "PSP STREAMER // %s", section);
    tv_receiver();
}

static void tv_help(const char *text) {
    /* Two native-font lines; no text is drawn over the physical controls. */
    tv_rect(&tv_canvas, 29, 317, 665, 34, 0x000C0C0A);
    tv_text(34, 318, 50, 2, TV_WHITE, "%s", text);
}

static void tv_present(void) {
    if (!tv_ui_active || !display_output.tv || tvout_video_active || !tv_canvas.pixels) return;
    /* The proven scanout uses real EDRAM, never RAM or the extra EDRAM alias.
     * One staged frame avoids exposing partially drawn menus to scanout. */
    sceDisplayWaitVblankStart();
    memcpy((void *)0x44000000, tv_canvas.pixels, TV_GUI_BYTES);
    sceDisplaySetFrameBuf((void *)0x04000000, TV_GUI_STRIDE,
                         PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTVSYNC);
}

static void tv_draw_view(int view, int selected, int row, int audio_only,
                         const char *title, int fullscreen) {
    int i;
    const char *section = tr(view == TV_VIEW_LIBRARY ? TXT_MEDIA_LIBRARY :
        view == TV_VIEW_LOADING ? TXT_PREPARING_MEDIA : view == TV_VIEW_INFO ? TXT_FILE_DETAILS :
        view == TV_VIEW_OPTIONS ? TXT_STREAM_OPTIONS : TXT_NOW_PLAYING);
    if (!tv_ui_active || tvout_video_active || !tv_canvas.pixels) return;
    tv_shell(section);
    if (view == TV_VIEW_LIBRARY) {
        int first = item_count ? selected / TV_GUI_ROWS * TV_GUI_ROWS : 0;
        tv_text(34, 65, 38, 2, TV_MUTED, "%s", current_path[0] ? current_path : "/");
        if (!item_count) tv_text(34, 111, 38, 2, TV_WHITE, "%s", tr(TXT_NO_ENTRIES));
        for (i = first; i < item_count && i < first + TV_GUI_ROWS; i++) {
            int y = 101 + (i - first) * 16;
            if (i == selected) tv_rect(&tv_canvas, 32, y, 498, 16, 0x003B4824);
            tv_text(34, y, 38, 1, i == selected ? TV_WHITE : TV_CYAN, "%c %s",
                    items[i].is_folder ? '+' : items[i].is_audio ? '~' : '>', items[i].title);
        }
        tv_text(562, 66, 10, 1, TV_AMBER, "%s", tr(TXT_SELECTED));
        tv_text(562, 91, 10, 7, TV_WHITE, "%s", item_count ? items[selected].title : tr(TXT_WAITING));
        tv_text(562, 209, 10, 2, TV_MUTED, tr(TXT_ENTRIES), item_count);
        tv_text(562, 244, 10, 3, TV_CYAN, "%s", status);
        tv_help(tr(TXT_LIBRARY_CONTROLS));
    } else if (view == TV_VIEW_LOADING) {
        tv_text(34, 67, 38, 2, TV_AMBER, "%s", tr(TXT_READING_MEDIA));
        tv_text(34, 121, 38, 3, TV_WHITE, "%s", tr(TXT_LOADING_TRACKS));
        tv_text(34, 197, 38, 3, TV_MUTED, "%s", tr(TXT_SOURCE_WAKING));
        tv_text(562, 67, 10, 3, TV_AMBER, "%s", tr(TXT_PLEASE_WAIT));
    } else if (view == TV_VIEW_INFO) {
        tv_text(34, 66, 38, 3, TV_WHITE, "%s", items[selected].title);
        tv_text(34, 132, 38, 1, TV_CYAN, "%s", items[selected].is_audio ? tr(TXT_MUSIC_STREAM) : tr(TXT_VIDEO_STREAM));
        if (current_duration_seconds > 0.0f)
            tv_text(34, 160, 38, 1, TV_MUTED, tr(TXT_DURATION), (int)current_duration_seconds / 60, (int)current_duration_seconds % 60);
        else tv_text(34, 160, 38, 2, TV_MUTED, "%s", tr(TXT_DURATION_UNKNOWN));
        tv_text(34, 200, 38, 1, TV_MUTED, tr(TXT_AUDIO_TRACKS), audio_track_count);
        tv_text(34, 228, 38, 1, TV_MUTED, tr(TXT_SUBTITLE_TRACKS), subtitle_track_count);
        tv_text(562, 66, 10, 1, TV_AMBER, "%s", tr(TXT_STREAMS));
        for (i = 0; i < audio_track_count && i < 6; i++)
            tv_text(562, 94 + i * 18, 10, 1, TV_WHITE, "A%d %s", i + 1, audio_tracks[i].language);
        for (i = 0; i < subtitle_track_count && i + audio_track_count < 11; i++)
            tv_text(562, 94 + (i + audio_track_count) * 18, 10, 1, TV_CYAN, "S%d %s", i + 1, subtitle_tracks[i].language);
        tv_help(tr(TXT_INFO_CONTROLS));
    } else if (view == TV_VIEW_OPTIONS) {
        tv_text(34, 66, 38, 1, TV_CYAN, "%s", tr(TXT_STREAM_OPTIONS));
        tv_rect(&tv_canvas, 32, 105 + row * 60, 498, 48, 0x003B4824);
        if (audio_only) {
            tv_text(34, 110, 38, 2, TV_WHITE, "%s: %s", tr(TXT_QUALITY), audio_quality_name());
            tv_text(34, 170, 38, 2, TV_WHITE, "%s: %s", tr(TXT_PLAY_ORDER), tr(audio_shuffle ? TXT_SHUFFLE : TXT_SEQUENTIAL));
        } else {
            tv_text(34, 110, 38, 3, TV_WHITE, tr(TXT_AUDIO_LABEL),
                audio_track_count ? audio_tracks[selected_audio_track].language : tr(TXT_NOT_DETECTED),
                audio_track_count && audio_tracks[selected_audio_track].title[0] ? " - " : "",
                audio_track_count ? audio_tracks[selected_audio_track].title : "");
            tv_text(34, 170, 38, 3, TV_WHITE, tr(TXT_SUBS_LABEL),
                selected_subtitle_track < 0 ? tr(TXT_OFF) : subtitle_tracks[selected_subtitle_track].language,
                selected_subtitle_track >= 0 && subtitle_tracks[selected_subtitle_track].title[0] ? " - " : "",
                selected_subtitle_track >= 0 ? subtitle_tracks[selected_subtitle_track].title : "");
            tv_text(34, 230, 38, 2, TV_WHITE, "%s: %s", tr(TXT_QUALITY), audio_quality_name());
        }
        tv_text(562, 67, 10, 3, TV_AMBER, "%s", tr(audio_only ? TXT_QUALITY : TXT_AUDIO_SUB));
        tv_text(562, 132, 10, 1, TV_MUTED, "%s", tr(TXT_TV_SAVED_LINE1));
        tv_text(562, 150, 10, 1, TV_MUTED, "%s", tr(TXT_TV_SAVED_LINE2));
        tv_text(562, 168, 10, 1, TV_MUTED, "%s", tr(TXT_TV_SAVED_LINE3));
        tv_help(tr(audio_only ? TXT_MUSIC_SETUP_CONTROLS : TXT_VIDEO_SETUP_CONTROLS));
    } else {
        if (fullscreen) tv_rect(&tv_canvas, 25, 59, 673, 239, 0x000C0C0A);
        tv_text(34, 65, fullscreen ? 50 : 38, 2, TV_WHITE, "%s", title);
        if (!fullscreen) {
            tv_text(562, 67, 10, 3, TV_AMBER, "%s", tr(TXT_MUSIC));
            tv_text(562, 127, 10, 3, TV_MUTED, tr(TXT_TV_VOLUME), playback_volume * 100 / 30);
            tv_text(562, 194, 10, 3, TV_MUTED, "%s: %s", tr(TXT_QUALITY), audio_quality_name());
        }
        for (i = 0; i < SPECTRUM_BANDS; i++) {
            int target = !audio_running || !audio_start ? 0 : spectrum_levels[i];
            int height;
            spectrum_display[i] = music_ui_envelope(spectrum_display[i], target);
            height = spectrum_display[i] * 168 / 100;
            tv_rect(&tv_canvas, 37 + i * (fullscreen ? 54 : 41), 288 - height,
                    fullscreen ? 38 : 28, height, i < 4 ? TV_AMBER : i < 8 ? 0x00B070FF : TV_CYAN);
        }
        tv_help(tr(TXT_MUSIC_CONTROLS));
    }
    tv_present();
}

/* Music is an incremental scene. No second full-frame cache is required:
 * restore tiny moving controls from the immutable skin and update only the
 * gained/lost portion of each spectrum bar. Static text stays untouched. */
typedef struct { int x, y, width, height; } TvDirtyRect;
static struct {
    int valid, fullscreen, volume, meter[2], height[SPECTRUM_BANDS];
    u32 light[5];
    unsigned long long next_tick, copied_bytes;
    unsigned int full_frames, incremental_frames;
} tv_music;

static void tv_music_reset(void) { memset(&tv_music, 0, sizeof(tv_music)); }

static void tv_restore_rect(int x, int y, int width, int height) {
    int yy;
    for (yy = y; yy < y + height; yy++) {
        if (receiver_tv_skin_end - receiver_tv_skin == TV_GUI_WIDTH * TV_GUI_HEIGHT * 4)
            memcpy(tv_canvas.pixels + yy * TV_GUI_STRIDE + x,
                   receiver_tv_skin + (yy * TV_GUI_WIDTH + x) * 4, width * 4);
        else tv_rect(&tv_canvas, x, yy, width, 1, 0);
    }
}

static void tv_draw_music(const char *title, int fullscreen) {
    TvDirtyRect dirty[SPECTRUM_BANDS + 10];
    int count = 0, i;
    unsigned int parts = 0;
    unsigned long long now = sceKernelGetSystemTimeWide();
    if (!tv_ui_active || !display_output.tv || tvout_video_active || !tv_canvas.pixels) return;
    if (!tv_music.valid || tv_music.fullscreen != fullscreen) {
        tv_draw_view(TV_VIEW_MUSIC, 0, 0, 1, title, fullscreen);
        tv_music.valid = 1; tv_music.fullscreen = fullscreen;
        tv_music.volume = playback_volume;
        tv_music.meter[0] = (vu_display_left * 20 + 50) / 100;
        tv_music.meter[1] = (vu_display_right * 20 + 50) / 100;
        for (i = 0; i < 5; i++) tv_music.light[i] = tv_indicator_color(i);
        for (i = 0; i < SPECTRUM_BANDS; i++) tv_music.height[i] = spectrum_display[i] * 168 / 100;
        tv_music.next_tick = sceKernelGetSystemTimeWide() + MUSIC_UI_INTERVAL_US;
        tv_music.copied_bytes += TV_GUI_BYTES;
        tv_music.full_frames++;
        return;
    }
    /* Bound visual work to 20 Hz. Input polling and the DAC remain separate. */
    if (now < tv_music.next_tick) return;
    vu_ballistics_step();
    for (i = 0; i < 2; i++) {
        int meter = ((i ? vu_display_right : vu_display_left) * 20 + 50) / 100;
        if (meter != tv_music.meter[i]) {
            TvDirtyRect rect = {i ? 193 : 46, 388, 90, 47};
            tv_restore_rect(rect.x, rect.y, rect.width, rect.height);
            dirty[count++] = rect;
            parts |= 1U << i; tv_music.meter[i] = meter;
        }
    }
    for (i = 0; i < 5; i++) {
        u32 color = tv_indicator_color(i);
        if (color != tv_music.light[i]) {
            dirty[count++] = (TvDirtyRect){331 + i * 47, 387, 25, 2};
            tv_music.light[i] = color; parts |= 1U << (i + 2);
        }
    }
    if (tv_music.volume != playback_volume) {
        TvDirtyRect knob = {603, 375, 61, 66}, label = {607, 452, 78, 16};
        tv_restore_rect(knob.x, knob.y, knob.width, knob.height);
        tv_restore_rect(label.x, label.y, label.width, label.height);
        dirty[count++] = knob; dirty[count++] = label;
        parts |= 128; tv_music.volume = playback_volume;
        if (!fullscreen) {
            TvDirtyRect side = {562, 127, 130, 48};
            tv_restore_rect(side.x, side.y, side.width, side.height);
            tv_text(562, 127, 10, 3, TV_MUTED, tr(TXT_TV_VOLUME), playback_volume * 100 / 30);
            dirty[count++] = side;
        }
    }
    tv_receiver_parts(parts);
    for (i = 0; i < SPECTRUM_BANDS; i++) {
        int target = !audio_running || !audio_start ? 0 : spectrum_levels[i];
        int height, previous = tv_music.height[i];
        int x = 37 + i * (fullscreen ? 54 : 41), width = fullscreen ? 38 : 28;
        spectrum_display[i] = music_ui_envelope(spectrum_display[i], target);
        height = spectrum_display[i] * 168 / 100;
        if (height > previous) {
            tv_rect(&tv_canvas, x, 288 - height, width, height - previous,
                    i < 4 ? TV_AMBER : i < 8 ? 0x00B070FF : TV_CYAN);
            dirty[count++] = (TvDirtyRect){x, 288 - height, width, height - previous};
        } else if (height < previous) {
            if (fullscreen) tv_rect(&tv_canvas, x, 288 - previous, width, previous - height, 0x000C0C0A);
            else tv_restore_rect(x, 288 - previous, width, previous - height);
            dirty[count++] = (TvDirtyRect){x, 288 - previous, width, previous - height};
        }
        tv_music.height[i] = height;
    }
    if (count) {
        u32 *vram = (u32 *)0x44000000;
        sceDisplayWaitVblankStart();
        for (i = 0; i < count; i++) {
            TvDirtyRect rect = dirty[i];
            int y;
            for (y = rect.y; y < rect.y + rect.height; y++)
                memcpy(vram + y * TV_GUI_STRIDE + rect.x,
                       tv_canvas.pixels + y * TV_GUI_STRIDE + rect.x, rect.width * 4);
            tv_music.copied_bytes += (unsigned int)(rect.width * rect.height * 4);
        }
        /* Address/stride stay fixed: no display API or mode reset is needed. */
    }
    tv_music.incremental_frames++;
    tv_music.next_tick = sceKernelGetSystemTimeWide() + MUSIC_UI_INTERVAL_US;
}
#endif
