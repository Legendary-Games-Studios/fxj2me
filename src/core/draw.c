/* fxj2me: 1-bit drawing implementation. Clipping is uniform: every write
 * bounds-checks against the surface and the clip rectangle; reads only
 * bounds-check against the surface. */
#include "draw.h"
#include <string.h>

extern const uint8_t fx_font8x8[FX_FONT_GLYPHS][8];   /* font8x8.c */

/* sin(d) for d = 0..90 in 0..256 fixed point (integer-only rendering). */
static const uint16_t fx_sin90[91] = {
      0,   4,   9,  13,  18,  22,  27,  31,  36,  40,  44,  49,
     53,  58,  62,  66,  71,  75,  79,  83,  88,  92,  96, 100,
    104, 108, 112, 116, 120, 124, 128, 132, 136, 139, 143, 147,
    150, 154, 158, 161, 165, 168, 171, 175, 178, 181, 184, 187,
    190, 193, 196, 199, 202, 204, 207, 210, 212, 215, 217, 219,
    222, 224, 226, 228, 230, 232, 234, 236, 237, 239, 241, 242,
    243, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 254,
    255, 255, 255, 256, 256, 256, 256,
};

/* sin/cos of any angle in -256..256. */
static int fx_isin(int deg)
{
    deg %= 360;
    if (deg < 0)
        deg += 360;
    if (deg <= 90)  return (int)fx_sin90[deg];
    if (deg <= 180) return (int)fx_sin90[180 - deg];
    if (deg <= 270) return -(int)fx_sin90[deg - 180];
    return -(int)fx_sin90[360 - deg];
}

static int fx_icos(int deg)
{
    return fx_isin(deg + 90);
}

static int fx_isqrt(unsigned v)
{
    unsigned r = 0, bit = 1u << 30;
    while (bit > v)
        bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (int)r;
}

void fx_init(fx_surface *s, uint8_t *bits, int w, int h)
{
    s->bits = bits;
    s->w = w;
    s->h = h;
    s->cx = 0;
    s->cy = 0;
    s->cw = w;
    s->ch = h;
}

void fx_set_clip(fx_surface *s, int x, int y, int w, int h)
{
    int x1 = x + w, y1 = y + h;

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    s->cx = x;
    s->cy = y;
    s->cw = (x1 > x) ? x1 - x : 0;
    s->ch = (y1 > y) ? y1 - y : 0;
}

void fx_clear(fx_surface *s, int color)
{
    fx_fill_rect(s, 0, 0, s->w, s->h, color);
}

void fx_pixel(fx_surface *s, int x, int y, int color)
{
    size_t i;
    if (x < s->cx || y < s->cy ||
        x >= s->cx + s->cw || y >= s->cy + s->ch)
        return;
    i = fx_stride(s) * (size_t)y + (size_t)(x >> 3);
    if (color)
        s->bits[i] |= (uint8_t)(0x80 >> (x & 7));
    else
        s->bits[i] &= (uint8_t)~(0x80 >> (x & 7));
}

int fx_pixel_get(const fx_surface *s, int x, int y)
{
    size_t i;
    if (x < 0 || y < 0 || x >= s->w || y >= s->h)
        return 0;
    i = fx_stride(s) * (size_t)y + (size_t)(x >> 3);
    return (s->bits[i] >> (7 - (x & 7))) & 1;
}

void fx_fill_rect(fx_surface *s, int x, int y, int w, int h, int color)
{
    int x1, y1, px, py;

    if (w <= 0 || h <= 0)
        return;
    x1 = x + w;
    y1 = y + h;
    /* intersect with the clip rect */
    if (x < s->cx) x = s->cx;
    if (y < s->cy) y = s->cy;
    if (x1 > s->cx + s->cw) x1 = s->cx + s->cw;
    if (y1 > s->cy + s->ch) y1 = s->cy + s->ch;

    for (py = y; py < y1; py++) {
        /* Fast path: whole aligned bytes get memset. */
        px = x;
        if (color) {
            while (px < x1 && (px & 7) != 0)
                fx_pixel(s, px++, py, 1);
            while (px + 8 <= x1) {
                s->bits[fx_stride(s) * (size_t)py + (size_t)(px >> 3)] = 0xff;
                px += 8;
            }
            while (px < x1)
                fx_pixel(s, px++, py, 1);
        } else {
            while (px < x1 && (px & 7) != 0)
                fx_pixel(s, px++, py, 0);
            while (px + 8 <= x1) {
                s->bits[fx_stride(s) * (size_t)py + (size_t)(px >> 3)] = 0x00;
                px += 8;
            }
            while (px < x1)
                fx_pixel(s, px++, py, 0);
        }
    }
}

void fx_draw_rect(fx_surface *s, int x, int y, int w, int h, int color)
{
    if (w <= 0 || h <= 0)
        return;
    fx_fill_rect(s, x, y, w, 1, color);
    fx_fill_rect(s, x, y + h - 1, w, 1, color);
    fx_fill_rect(s, x, y, 1, h, color);
    fx_fill_rect(s, x + w - 1, y, 1, h, color);
}

void fx_draw_line(fx_surface *s, int x0, int y0, int x1, int y1, int color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    for (;;) {
        int e2;
        fx_pixel(s, x0, y0, color);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/*--- rounded rects -----------------------------------------------------*/

/* Horizontal inset of row j (0..ah-1) of a corner with arc (aw, ah). */
static int rr_inset(int aw, int ah, int j)
{
    int m = ah - j;                     /* 1..ah */
    int r2 = ah * ah - m * m;
    return aw - aw * fx_isqrt((unsigned)(r2 > 0 ? r2 : 0)) / ah;
}

void fx_fill_round_rect(fx_surface *s, int x, int y, int w, int h,
                        int aw, int ah, int color)
{
    int j;

    if (w <= 0 || h <= 0)
        return;
    if (aw <= 0 || ah <= 0) {
        fx_fill_rect(s, x, y, w, h, color);
        return;
    }
    if (aw * 2 > w) aw = w / 2;
    if (ah * 2 > h) ah = h / 2;
    if (aw <= 0 || ah <= 0) {
        fx_fill_rect(s, x, y, w, h, color);
        return;
    }

    for (j = 0; j < h; j++) {
        int inset = 0;
        if (j < ah)
            inset = rr_inset(aw, ah, j);
        else if (j >= h - ah)
            inset = rr_inset(aw, ah, h - 1 - j);
        fx_fill_rect(s, x + inset, y + j, w - 2 * inset, 1, color);
    }
}

void fx_draw_round_rect(fx_surface *s, int x, int y, int w, int h,
                        int aw, int ah, int color)
{
    int j;

    if (w <= 0 || h <= 0)
        return;
    if (aw <= 0 || ah <= 0) {
        fx_draw_rect(s, x, y, w, h, color);
        return;
    }
    if (aw * 2 > w) aw = w / 2;
    if (ah * 2 > h) ah = h / 2;

    for (j = 0; j < h; j++) {
        int inset = 0;
        if (j < ah)
            inset = rr_inset(aw, ah, j);
        else if (j >= h - ah)
            inset = rr_inset(aw, ah, h - 1 - j);
        fx_pixel(s, x + inset, y + j, color);
        fx_pixel(s, x + w - 1 - inset, y + j, color);
    }
    fx_draw_line(s, x + aw, y, x + w - aw - 1, y, color);
    fx_draw_line(s, x + aw, y + h - 1, x + w - aw - 1, y + h - 1, color);
}

/*--- arcs --------------------------------------------------------------*/

void fx_draw_arc(fx_surface *s, int x, int y, int w, int h,
                 int start, int arc, int color)
{
    int a, b, cx0, cy0, t, n;
    int px = 0, py = 0, have_prev = 0;

    if (w <= 0 || h <= 0 || arc == 0)
        return;
    if (arc < 0) {
        start += arc;
        arc = -arc;
    }
    if (arc > 360)
        arc = 360;
    a = w / 2;
    b = h / 2;
    if (a < 1) a = 1;
    if (b < 1) b = 1;
    cx0 = x + w / 2;
    cy0 = y + h / 2;

    n = arc;
    for (t = 0; t <= n; t++) {
        int qx = cx0 + a * fx_icos(start + t) / 256;
        int qy = cy0 - b * fx_isin(start + t) / 256;
        if (have_prev)
            fx_draw_line(s, px, py, qx, qy, color);
        else
            fx_pixel(s, qx, qy, color);
        px = qx;
        py = qy;
        have_prev = 1;
    }
}

/* Wedge test in unit-circle space: is (X, Y) between rays at s and s+arc
 * going counterclockwise? arc in 1..360, coordinates scaled to -256..256. */
static int in_wedge(int X, int Y, int s, int arc)
{
    long cs = fx_icos(s), sn = fx_isin(s);
    long ce = fx_icos(s + arc), se = fx_isin(s + arc);
    long c1 = cs * (long)Y - sn * (long)X;      /* cross(D(s), P) */
    long c2 = (long)X * se - (long)Y * ce;      /* cross(P, D(e)) */

    if (arc >= 360)
        return 1;
    if (arc <= 180)
        return c1 >= 0 && c2 >= 0;
    /* wide wedge: inside unless the pixel is in the small complement */
    return !(c1 < 0 && c2 < 0);
}

void fx_fill_arc(fx_surface *s, int x, int y, int w, int h,
                 int start, int arc, int color)
{
    int a, b, cx0, cy0, px, py;
    int xs, ys, xe, ye;

    if (w <= 0 || h <= 0 || arc == 0)
        return;
    if (arc < 0) {
        start += arc;
        arc = -arc;
    }
    if (arc > 360)
        arc = 360;
    a = w / 2;
    b = h / 2;
    if (a < 1) a = 1;
    if (b < 1) b = 1;
    cx0 = x + w / 2;
    cy0 = y + h / 2;

    xs = x > s->cx ? x : s->cx;
    ys = y > s->cy ? y : s->cy;
    xe = x + w < s->cx + s->cw ? x + w : s->cx + s->cw;
    ye = y + h < s->cy + s->ch ? y + h : s->cy + s->ch;

    for (py = ys; py < ye; py++) {
        for (px = xs; px < xe; px++) {
            int X = (px - cx0) * 256 / a;
            int Y = -(py - cy0) * 256 / b;
            if (X * X + Y * Y > 256 * 256)
                continue;                    /* outside the ellipse */
            if (in_wedge(X, Y, start, arc))
                fx_pixel(s, px, py, color);
        }
    }
}

/*--- triangle ----------------------------------------------------------*/

static int tri_lerp(int x1, int x2, int t, int d)
{
    return x1 + (int)((int64_t)(x2 - x1) * t / d);
}

void fx_fill_triangle(fx_surface *s, int x1, int y1, int x2, int y2,
                      int x3, int y3, int color)
{
    int y;

    /* sort vertices by y (x follows) */
    if (y1 > y2) { int t; t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
    if (y2 > y3) { int t; t = y2; y2 = y3; y3 = t; t = x2; x2 = x3; x3 = t; }
    if (y1 > y2) { int t; t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }

    if (y1 == y3) {                          /* degenerate: flat line */
        int lo = x1 < x2 ? x1 : x2;
        int hi = x1 > x2 ? x1 : x2;
        if (x3 < lo) lo = x3;
        if (x3 > hi) hi = x3;
        fx_fill_rect(s, lo, y1, hi - lo + 1, 1, color);
        return;
    }

    for (y = y1; y <= y3; y++) {
        int xa, xb;
        xa = tri_lerp(x1, x3, y - y1, y3 - y1);
        if (y <= y2 && y2 > y1)
            xb = tri_lerp(x1, x2, y - y1, y2 - y1);
        else if (y3 > y2)
            xb = tri_lerp(x2, x3, y - y2, y3 - y2);
        else
            xb = x2;
        if (xb < xa) { int t = xa; xa = xb; xb = t; }
        fx_fill_rect(s, xa, y, xb - xa + 1, 1, color);
    }
}

/*--- text --------------------------------------------------------------*/

void fx_draw_char(fx_surface *s, int x, int y, int ch, int color)
{
    const uint8_t *g;
    int row, col;

    if (ch < 32 || ch > 126)
        ch = '?';
    g = fx_font8x8[ch - 32];
    for (row = 0; row < 8; row++) {
        uint8_t bits = g[row];
        if (!bits)
            continue;
        for (col = 0; col < 8; col++)
            if (bits & (0x80 >> col))
                fx_pixel(s, x + col, y + row, color);
    }
}

void fx_draw_string(fx_surface *s, int x, int y, const uint16_t *text,
                    int len, int color)
{
    int i;
    for (i = 0; i < len; i++) {
        fx_draw_char(s, x, y, (int)text[i], color);
        x += FX_FONT_ADVANCE;
    }
}

void fx_blit(fx_surface *dst, int dx, int dy,
             const fx_surface *src, int sx, int sy, int w, int h,
             int transparent)
{
    int x, y;

    /* Clip the source rectangle against both surfaces. */
    if (sx < 0) {
        w += sx;
        dx -= sx;
        sx = 0;
    }
    if (sy < 0) {
        h += sy;
        dy -= sy;
        sy = 0;
    }
    if (sx + w > src->w) w = src->w - sx;
    if (sy + h > src->h) h = src->h - sy;
    if (w <= 0 || h <= 0)
        return;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int p = fx_pixel_get(src, sx + x, sy + y);
            if (transparent && !p)
                continue;
            fx_pixel(dst, dx + x, dy + y, p);
        }
    }
}

void fx_scale_128x64(const fx_surface *src, uint8_t *dst)
{
    /* 4x4 Bayer matrix, scaled to 0..15 thresholds. */
    static const uint8_t bayer[4][4] = {
        { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
    };
    int dx, dy;

    memset(dst, 0, FX_SCREEN_W * FX_SCREEN_H / 8);
    for (dy = 0; dy < FX_SCREEN_H; dy++) {
        for (dx = 0; dx < FX_SCREEN_W; dx++) {
            /* Box filter over the source region feeding this pixel. */
            int sx0 = (int)((int64_t)dx * src->w / FX_SCREEN_W);
            int sx1 = (int)((int64_t)(dx + 1) * src->w / FX_SCREEN_W);
            int sy0 = (int)((int64_t)dy * src->h / FX_SCREEN_H);
            int sy1 = (int)((int64_t)(dy + 1) * src->h / FX_SCREEN_H);
            int total, black, x, y;

            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sy1 <= sy0) sy1 = sy0 + 1;

            total = 0;
            black = 0;
            for (y = sy0; y < sy1 && y < src->h; y++)
                for (x = sx0; x < sx1 && x < src->w; x++) {
                    total++;
                    black += fx_pixel_get(src, x, y);
                }
            if (total == 0)
                continue;

            /* Threshold with ordered dither: decide black when the covered
             * black fraction passes the Bayer threshold. 16 levels on the
             * fraction, +8 biases 50% coverage to black (no dithering on
             * solid areas). */
            if (black * 16 + 8 >= total * (bayer[dy & 3][dx & 3] * 2 + 1))
                dst[(dy * FX_SCREEN_W + dx) >> 3] |=
                    (uint8_t)(0x80 >> (dx & 7));
        }
    }
}
