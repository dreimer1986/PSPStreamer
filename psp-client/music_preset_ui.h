#ifndef PSPSTREAMER_MUSIC_PRESET_UI_H
#define PSPSTREAMER_MUSIC_PRESET_UI_H
/* Only called for the custom slot after a failed pre-playback file load.
 * Never changes scanout configuration or triggers another disk read. */
static void music_preset_notice(const MdFileError *error, int fullscreen) {
    TextId id = error->code == MD_FILE_MISSING ? TXT_PRESET_MISSING :
                error->code == MD_FILE_UNSUPPORTED ? TXT_PRESET_UNSUPPORTED :
                error->code == MD_FILE_IO ? TXT_PRESET_IO : TXT_PRESET_INVALID;
    char detail[48];
    if (error->line) snprintf(detail, sizeof(detail), "L%d: %.27s", error->line, error->key);
    else strcpy(detail, "presets/active.milk");
    if (tv_ui_active) {
        u32 *vram = (u32 *)0x44000000;
        int y;
        tv_rect(&tv_canvas, 34, 98, 504, 32, 0x000C0C0A);
        tv_text(34, 98, 38, 1, TV_AMBER, "%s", tr(id));
        tv_text(34, 114, 38, 1, TV_WHITE, "%s", detail);
        for (y = 98; y < 130; y++)
            memcpy(vram + y*TV_GUI_STRIDE + 34, tv_canvas.pixels + y*TV_GUI_STRIDE + 34, 504*4);
    } else {
        int y = fullscreen ? 32 : 160;
        gui_rect((u32 *)0x44000000, 38, y, 400, 16, 0x00080E14);
        gui_text(38, y, 0x0000D8FF, "%s", tr(id));
        gui_text(38, y+8, 0x00FFFFFF, "%s", detail);
    }
}
#endif
