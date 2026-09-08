/* Native TV drawing primitives. Coordinates are pixels, never LCD-scaled.
 * RAM staging has the same 768-pixel pitch as the TV scanout. */
#ifndef PSP_STREAMER_TV_CANVAS_H
#define PSP_STREAMER_TV_CANVAS_H
#include <stdint.h>
#include <stdlib.h>
#define TV_GUI_WIDTH 720
#define TV_GUI_HEIGHT 480
#define TV_GUI_STRIDE 768
#define TV_GUI_BYTES (TV_GUI_STRIDE * TV_GUI_HEIGHT * 4)
#define TV_GUI_ROWS 12
typedef struct { uint32_t *pixels; } TvCanvas;

static void tv_pixel(TvCanvas *canvas, int x, int y, uint32_t color) {
    if (x >= 0 && x < TV_GUI_WIDTH && y >= 0 && y < TV_GUI_HEIGHT)
        canvas->pixels[y * TV_GUI_STRIDE + x] = color;
}
static void tv_rect(TvCanvas *canvas, int x, int y, int width, int height, uint32_t color) {
    int xx, yy;
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (width > TV_GUI_WIDTH - x) width = TV_GUI_WIDTH - x;
    if (height > TV_GUI_HEIGHT - y) height = TV_GUI_HEIGHT - y;
    for (yy = y; yy < y + height; yy++)
        for (xx = x; xx < x + width; xx++) tv_pixel(canvas, xx, yy, color);
}
static void tv_line(TvCanvas *canvas, int x, int y, int end_x, int end_y, uint32_t color) {
    int dx = abs(end_x - x), sx = x < end_x ? 1 : -1;
    int dy = -abs(end_y - y), sy = y < end_y ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        int twice = 2 * error;
        tv_pixel(canvas, x, y, color);
        if (x == end_x && y == end_y) break;
        if (twice >= dy) { error += dy; x += sx; }
        if (twice <= dx) { error += dx; y += sy; }
    }
}
/* Font atlas already contains native 12x16 glyphs in 16x20 cells.
 * LCD downsamples them; TV uses their original pixels and antialiasing. */
static void tv_glyph(TvCanvas *canvas, const unsigned char *font, int glyph,
                     int x, int y, uint32_t color) {
    int xx, yy;
    const unsigned char *bitmap;
    if (!font || glyph < 0 || glyph > 255) return;
    bitmap = font + (glyph >> 4) * 20 * 256 + (glyph & 15) * 16;
    for (yy = 0; yy < 16; yy++) for (xx = 0; xx < 12; xx++) {
        unsigned int a = bitmap[yy * 256 + xx];
        int px = x + xx, py = y + yy;
        uint32_t old, blended = 0;
        int shift;
        if (!a || px < 0 || px >= TV_GUI_WIDTH || py < 0 || py >= TV_GUI_HEIGHT) continue;
        old = canvas->pixels[py * TV_GUI_STRIDE + px];
        for (shift = 0; shift < 24; shift += 8)
            blended |= ((((old >> shift) & 255) * (255 - a) +
                         ((color >> shift) & 255) * a + 127) / 255) << shift;
        tv_pixel(canvas, px, py, blended);
    }
}
#endif
