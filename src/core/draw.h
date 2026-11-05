/* fxj2me: 1-bit drawing — primitives, 6x8 bitmap font, downscaler.
 *
 * Surfaces are packed 1bpp (bit 7 of each byte = leftmost pixel), stride
 * (w+7)/8. Bit set = black (foreground), matching the fx-9860 VRAM sense.
 *
 * Clipping is uniform: every write is bounds-checked against the surface
 * *and* against the clip rectangle (set with fx_set_clip; fx_init starts
 * with a full-surface clip). Reads (fx_pixel_get) are only bounds-checked.
 */
#ifndef FXJ2ME_DRAW_H
#define FXJ2ME_DRAW_H

#include "fxj2me.h"

#define FX_FONT_GLYPHS  95    /* ASCII 32..126 */
#define FX_FONT_ADVANCE 6
#define FX_FONT_HEIGHT  8
#define FX_FONT_ASCENT  6     /* rows 0..5 sit above the baseline */

extern const uint8_t fx_font8x8[FX_FONT_GLYPHS][8];

typedef struct fx_surface {
    uint8_t *bits;
    int w, h;
    int cx, cy, cw, ch;   /* write-clip rectangle (always within bounds) */
} fx_surface;

/* Byte offset of row y in a packed surface. */
static inline size_t fx_stride(const fx_surface *s)
{
    return (size_t)((s->w + 7) / 8);
}

/* Bind bits to a surface with a full-surface clip. */
void fx_init(fx_surface *s, uint8_t *bits, int w, int h);

/* Set the write-clip rectangle (clamped to the surface; empty is allowed). */
void fx_set_clip(fx_surface *s, int x, int y, int w, int h);

void fx_clear(fx_surface *s, int color);
void fx_pixel(fx_surface *s, int x, int y, int color);
int fx_pixel_get(const fx_surface *s, int x, int y);

void fx_fill_rect(fx_surface *s, int x, int y, int w, int h, int color);
void fx_draw_rect(fx_surface *s, int x, int y, int w, int h, int color);
void fx_draw_line(fx_surface *s, int x0, int y0, int x1, int y1, int color);

/* Corners: (aw, ah) are the arc diameters; 0 falls back to a plain rect. */
void fx_draw_round_rect(fx_surface *s, int x, int y, int w, int h,
                        int aw, int ah, int color);
void fx_fill_round_rect(fx_surface *s, int x, int y, int w, int h,
                        int aw, int ah, int color);

/* Ellipse arcs on the bounding rect. Angles are degrees: 0 = 3 o'clock,
 * positive counterclockwise (MIDP semantics). Negative arc length draws
 * clockwise from start. fill_arc fills the pie sector. */
void fx_draw_arc(fx_surface *s, int x, int y, int w, int h,
                 int start, int arc, int color);
void fx_fill_arc(fx_surface *s, int x, int y, int w, int h,
                 int start, int arc, int color);

void fx_fill_triangle(fx_surface *s, int x1, int y1, int x2, int y2,
                      int x3, int y3, int color);

/* (x, y) is the top-left of the 8x8 glyph cell. */
void fx_draw_char(fx_surface *s, int x, int y, int ch, int color);
void fx_draw_string(fx_surface *s, int x, int y, const uint16_t *text,
                    int len, int color);

/* Blit a rectangle between surfaces. transparent: skip source pixels that
 * are white (0). Coordinates are clipped on both sides. */
void fx_blit(fx_surface *dst, int dx, int dy,
             const fx_surface *src, int sx, int sy, int w, int h,
             int transparent);

/* Box-filter scale any surface to exactly 128x64 with 4x4 Bayer dithering
 * (dst must hold 128*64/8 = 1024 bytes). */
#define FX_SCREEN_W 128
#define FX_SCREEN_H 64
void fx_scale_128x64(const fx_surface *src, uint8_t *dst);

#endif /* FXJ2ME_DRAW_H */
