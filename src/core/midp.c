/* fxj2me: MIDP runtime — lcdui natives, PNG decode, MIDlet launcher/pump.
 *
 * Rendering goes through the 1-bit draw core: every Graphics op resolves
 * its target Image's GC-managed pixel array and draws with the clip rect
 * and translate the Java-side state carries. The pump is cooperative and
 * host-portable: callSerially runnables, repaint dispatch into the 128x64
 * frame, key delivery to the current Canvas. */
#include "midp.h"
#include "classfile.h"
#include "draw.h"
#include "inflate.h"
#include "natives.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*--- launcher state ----------------------------------------------------*/

static struct {
    vm *v;
    jthread *t;
    jref screen;            /* the 128x64 Image (GC root)              */
    jref midlet;            /* the MIDlet instance (GC root)           */
    jref scratch[4];        /* registered transient roots for C calls  */
    jref last_canvas;       /* Displayable behind the current frame    */
    int finished;
    char err[128];
} st;

static int roots_registered;

static void ensure_roots(vm *v)
{
    if (roots_registered)
        return;
    heap_root_add(&v->heap, &st.screen);
    heap_root_add(&v->heap, &st.midlet);
    heap_root_add(&v->heap, &st.scratch[0]);
    heap_root_add(&v->heap, &st.scratch[1]);
    heap_root_add(&v->heap, &st.scratch[2]);
    heap_root_add(&v->heap, &st.scratch[3]);
    roots_registered = 1;
}

/*--- field access ------------------------------------------------------*/

static jslot *obj_fields(vm *v, jref o)
{
    return (jslot *)heap_ptr(&v->heap, o);
}

static int32_t fld_int(vm *v, jref o, const char *name)
{
    uint32_t slot;
    jclass *c = heap_hdr(&v->heap, o)->cls;
    if (!c || class_find_field(c, name, "I", false, &slot, NULL) != FX_OK)
        return 0;
    return obj_fields(v, o)[slot].i;
}

static jref fld_ref(vm *v, jref o, const char *name, const char *desc)
{
    uint32_t slot;
    bool is_ref;
    jclass *c = heap_hdr(&v->heap, o)->cls;
    if (!c || class_find_field(c, name, desc, false, &slot, &is_ref) != FX_OK)
        return JREF_NULL;
    return obj_fields(v, o)[slot].r;
}

static void set_int(vm *v, jref o, const char *name, int32_t val)
{
    uint32_t slot;
    jclass *c = heap_hdr(&v->heap, o)->cls;
    if (c && class_find_field(c, name, "I", false, &slot, NULL) == FX_OK)
        obj_fields(v, o)[slot].i = val;
}

static void set_ref(vm *v, jref o, const char *name, const char *desc,
                    jref val)
{
    uint32_t slot;
    bool is_ref;
    jclass *c = heap_hdr(&v->heap, o)->cls;
    if (c && class_find_field(c, name, desc, false, &slot, &is_ref) == FX_OK)
        obj_fields(v, o)[slot].r = val;
}


/*--- Java calls --------------------------------------------------------*/

static int m_call(vm *v, jthread *t, jref obj, const char *method,
                  const char *desc, const jslot *args, jslot *ret)
{
    jclass *c;
    jslot r[2];
    if (obj == JREF_NULL)
        return FX_ERR_NOTFOUND;
    c = heap_hdr(&v->heap, obj)->cls;
    if (!c)
        return FX_ERR_NOTFOUND;
    r[0].i = 0;
    r[1].i = 0;
    return vm_call(v, t, c, method, desc, obj, args, ret ? ret : r);
}

static int m_static(vm *v, jthread *t, const char *cls, const char *method,
                    const char *desc, const jslot *args, jslot *ret)
{
    jslot r[2];
    r[0].i = 0;
    r[1].i = 0;
    return vm_call_static(v, t, cls, method, desc, args, ret ? ret : r);
}

/*--- graphics plumbing -------------------------------------------------*/

static int grey_bit(int rgb)
{
    int r = (rgb >> 16) & 0xff;
    int g = (rgb >> 8) & 0xff;
    int b = rgb & 0xff;
    return (r * 30 + g * 59 + b * 11) / 100 < 128;
}

/* Resolve a Graphics object to a clipped surface + state. */
static int gfx_begin(vm *v, jref g, fx_surface *s, int *tx, int *ty,
                     int *color)
{
    jref target = fld_ref(v, g, "target",
                          "Ljavax/microedition/lcdui/Image;");
    jref pix;
    int w, h;

    if (target == JREF_NULL)
        return FX_ERR_NOTFOUND;
    pix = fld_ref(v, target, "pix", "[B");
    w = fld_int(v, target, "width");
    h = fld_int(v, target, "height");
    if (pix == JREF_NULL || w <= 0 || h <= 0)
        return FX_ERR_NOTFOUND;
    fx_init(s, (uint8_t *)heap_ptr(&v->heap, pix), w, h);
    fx_set_clip(s, fld_int(v, g, "clipX"), fld_int(v, g, "clipY"),
                fld_int(v, g, "clipW"), fld_int(v, g, "clipH"));
    *tx = fld_int(v, g, "translateX");
    *ty = fld_int(v, g, "translateY");
    *color = grey_bit(fld_int(v, g, "color"));
    return FX_OK;
}

/* Resolve an Image object to a full-clip surface. */
static int img_surface(vm *v, jref img, fx_surface *s)
{
    jref pix;
    int w, h;
    if (img == JREF_NULL)
        return FX_ERR_NOTFOUND;
    pix = fld_ref(v, img, "pix", "[B");
    w = fld_int(v, img, "width");
    h = fld_int(v, img, "height");
    if (pix == JREF_NULL || w <= 0 || h <= 0)
        return FX_ERR_NOTFOUND;
    fx_init(s, (uint8_t *)heap_ptr(&v->heap, pix), w, h);
    return FX_OK;
}

/*--- PNG decode --------------------------------------------------------*/

static uint32_t png_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc)
        return a;
    return (pb <= pc) ? b : c;
}

/* Sample channel k (0-based) of pixel x from an unfiltered row. */
static int png_sample(const uint8_t *row, int x, int ch, int depth, int k)
{
    int bit = (x * ch + k) * depth;
    if (depth == 16)
        return row[bit >> 3];                 /* big-endian high byte */
    if (depth == 8)
        return row[bit >> 3];
    return (row[bit >> 3] >> (8 - depth - (bit & 7))) & ((1 << depth) - 1);
}

static const uint8_t png_bayer[4][4] = {
    { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
};

/* Decode a PNG into a malloc'd 1bpp buffer (bit = black, MSB-first rows). */
static int png_decode(const uint8_t *d, size_t len,
                      uint8_t **out, int *ow, int *oh)
{
    static const uint8_t sig[8] = {
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'
    };
    uint8_t pal[768];
    int npal = 0;
    uint8_t *idat = NULL, *raw = NULL, *bits = NULL;
    size_t idat_len = 0, raw_need;
    int w = 0, h = 0, depth = 0, color = 0, ch = 0, rowbytes, bpp;
    int x, y, r = FX_ERR_FORMAT;
    size_t pos;

    if (len < 8 || memcmp(d, sig, 8) != 0)
        return FX_ERR_FORMAT;

    idat = malloc(len);
    if (!idat)
        return FX_ERR_NOSPACE;

    for (pos = 8; pos + 8 <= len; ) {
        uint32_t clen = png_u32(d + pos);
        const uint8_t *type = d + pos + 4;
        const uint8_t *body = d + pos + 8;
        if (clen > len || pos + 12 + (size_t)clen > len)
            goto done;

        if (memcmp(type, "IHDR", 4) == 0 && clen >= 13) {
            w = (int)png_u32(body);
            h = (int)png_u32(body + 4);
            depth = body[8];
            color = body[9];
            if (body[10] != 0 || body[11] != 0 || body[12] != 0)
                goto done;              /* compression/filter/interlace */
        } else if (memcmp(type, "PLTE", 4) == 0) {
            npal = (int)clen / 3;
            if (npal > 256)
                npal = 256;
            memcpy(pal, body, (size_t)npal * 3);
        } else if (memcmp(type, "IDAT", 4) == 0) {
            if (idat_len + clen > len)
                goto done;
            memcpy(idat + idat_len, body, clen);
            idat_len += clen;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        pos += 12 + (size_t)clen;
    }

    if (w <= 0 || h <= 0 || w > 2048 || h > 2048 || idat_len < 6)
        goto done;
    if (color == 0 || color == 3) ch = 1;
    else if (color == 2) ch = 3;
    else if (color == 4) ch = 2;
    else if (color == 6) ch = 4;
    else goto done;
    if (depth != 1 && depth != 2 && depth != 4 && depth != 8 &&
        depth != 16)
        goto done;
    if (depth < 8 && color != 0 && color != 3)
        goto done;
    if (color == 3 && npal == 0)
        goto done;

    rowbytes = (w * ch * depth + 7) / 8;
    bpp = (ch * depth + 7) / 8;
    if (bpp < 1)
        bpp = 1;
    raw_need = (size_t)h * ((size_t)rowbytes + 1);

    /* zlib stream: 2-byte header, raw deflate, 4-byte adler32 */
    if ((idat[0] & 0x0f) != 8 ||
        (((uint32_t)idat[0] << 8) | idat[1]) % 31 != 0)
        goto done;
    raw = malloc(raw_need);
    if (!raw) {
        r = FX_ERR_NOSPACE;
        goto done;
    }
    {
        inf_ctx c;
        inf_init(&c, idat + 2, idat_len - 6, raw, raw_need);
        if (inf_run(&c) != FX_OK || c.dstpos < raw_need)
            goto done;
    }

    /* unfilter rows in place */
    for (y = 0; y < h; y++) {
        uint8_t *cur = raw + (size_t)y * (rowbytes + 1) + 1;
        uint8_t *prev = y > 0 ? cur - (rowbytes + 1) : NULL;
        int ft = cur[-1];
        int i;
        for (i = 0; i < rowbytes; i++) {
            int a = i >= bpp ? cur[i - bpp] : 0;
            int b = prev ? prev[i] : 0;
            int c = prev && i >= bpp ? prev[i - bpp] : 0;
            switch (ft) {
            case 0: break;
            case 1: cur[i] = (uint8_t)(cur[i] + a); break;
            case 2: cur[i] = (uint8_t)(cur[i] + b); break;
            case 3: cur[i] = (uint8_t)(cur[i] + (a + b) / 2); break;
            case 4: cur[i] = (uint8_t)(cur[i] + paeth(a, b, c)); break;
            default: goto done;
            }
        }
    }

    /* convert to 1bpp with ordered dithering */
    {
        int stride = (w + 7) / 8;
        bits = calloc((size_t)stride, (size_t)h);
        if (!bits) {
            r = FX_ERR_NOSPACE;
            goto done;
        }
        for (y = 0; y < h; y++) {
            const uint8_t *row = raw + (size_t)y * (rowbytes + 1) + 1;
            for (x = 0; x < w; x++) {
                int lum;
                if (color == 0) {
                    int v = png_sample(row, x, 1, depth, 0);
                    lum = depth == 1 ? v * 255 :
                          depth == 2 ? v * 85 :
                          depth == 4 ? v * 17 : v;
                } else if (color == 2) {
                    int rr = png_sample(row, x, 3, depth, 0);
                    int gg = png_sample(row, x, 3, depth, 1);
                    int bb = png_sample(row, x, 3, depth, 2);
                    lum = (rr * 30 + gg * 59 + bb * 11) / 100;
                } else if (color == 3) {
                    int idx = png_sample(row, x, 1, depth, 0);
                    int rr, gg, bb;
                    if (idx >= npal)
                        idx = 0;
                    rr = pal[idx * 3];
                    gg = pal[idx * 3 + 1];
                    bb = pal[idx * 3 + 2];
                    lum = (rr * 30 + gg * 59 + bb * 11) / 100;
                } else if (color == 4) {
                    int v = png_sample(row, x, 2, depth, 0);
                    int a = png_sample(row, x, 2, depth, 1);
                    lum = (a < (1 << (depth - 1))) ? 255 : v;
                } else {
                    int rr = png_sample(row, x, 4, depth, 0);
                    int gg = png_sample(row, x, 4, depth, 1);
                    int bb = png_sample(row, x, 4, depth, 2);
                    int a = png_sample(row, x, 4, depth, 3);
                    lum = (a < (1 << (depth - 1))) ? 255 :
                          (rr * 30 + gg * 59 + bb * 11) / 100;
                }
                if (lum < png_bayer[y & 3][x & 3] * 16 + 8)
                    bits[(size_t)y * stride + (x >> 3)] |=
                        (uint8_t)(0x80 >> (x & 7));
            }
        }
    }

    *out = bits;
    *ow = w;
    *oh = h;
    bits = NULL;
    r = FX_OK;

done:
    free(idat);
    free(raw);
    free(bits);
    return r;
}

/* Build an immutable Image object around a 1bpp buffer. */
static jref make_image(vm *v, int w, int h, const uint8_t *bits)
{
    jclass *ic = vm_load_class(v, "javax/microedition/lcdui/Image");
    jref img, pix;
    size_t nbytes = (size_t)((w + 7) / 8) * (size_t)h;

    if (!ic)
        return JREF_NULL;
    img = vm_alloc(v, (size_t)ic->nslots * sizeof(jslot), ic, 0, 0);
    if (img == JREF_NULL)
        return JREF_NULL;
    v->tmp[0] = img;
    pix = vm_alloc(v, nbytes, NULL, JBLK_ARRAY, (uint32_t)nbytes);
    v->tmp[0] = JREF_NULL;
    if (pix == JREF_NULL)
        return JREF_NULL;
    memcpy(heap_ptr(&v->heap, pix), bits, nbytes);
    set_ref(v, img, "pix", "[B", pix);
    set_int(v, img, "width", w);
    set_int(v, img, "height", h);
    set_int(v, img, "mutable", 0);
    return img;
}

/*--- Graphics natives --------------------------------------------------*/

static int ng_drawLine(vm *v, jthread *t, jclass *oc,
                       const jslot *args, jslot *ret)
{
    fx_surface s;
    int tx, ty, col;
    (void)t; (void)oc; (void)ret;
    if (gfx_begin(v, args[0].r, &s, &tx, &ty, &col) != FX_OK)
        return FX_OK;
    fx_draw_line(&s, args[1].i + tx, args[2].i + ty,
                 args[3].i + tx, args[4].i + ty, col);
    return FX_OK;
}

static int ng_drawRect(vm *v, jthread *t, jclass *oc,
                       const jslot *args, jslot *ret)
{
    fx_surface s;
    int tx, ty, col;
    (void)t; (void)oc; (void)ret;
    if (gfx_begin(v, args[0].r, &s, &tx, &ty, &col) != FX_OK)
        return FX_OK;
    fx_draw_rect(&s, args[1].i + tx, args[2].i + ty, args[3].i,
                 args[4].i, col);
    return FX_OK;
}

static int ng_fillRect(vm *v, jthread *t, jclass *oc,
                       const jslot *args, jslot *ret)
{
    fx_surface s;
    int tx, ty, col;
    (void)t; (void)oc; (void)ret;
    if (gfx_begin(v, args[0].r, &s, &tx, &ty, &col) != FX_OK)
        return FX_OK;
    fx_fill_rect(&s, args[1].i + tx, args[2].i + ty, args[3].i,
                 args[4].i, col);
    return FX_OK;
}

static int ng_drawRoundRect(vm *v, jthread *t, jclass *oc,
                            const jslot *args, jslot *ret)
{
    fx_surface s;
    int tx, ty, col;
    (void)t; (void)oc; (void)ret;
    if (gfx_begin(v, args[0].r, &s, &tx, &ty, &col) != FX_OK)
        return FX_OK;
    fx_draw_round_rect(&s, args[1].i + tx, args[2].i + ty, args[3].i,
                       args[4].i, args[5].i, args[6].i, col);
    return FX_OK;
}

static int ng_fillRoundRect(vm *v, jthread *t, jclass *oc,
                            const jslot *args, jslot *ret)
{
    fx_surface s;
    int tx, ty, col;
    (void)t; (void)oc; (void)ret;
    if (gfx_begin(v, args[0].r, &s, &tx, &ty, &col) != FX_OK)
        return FX_OK;
    fx_fill_round_rect(&s, args[1].i + tx, args[2].i + ty, args[3].i,
                       args[4].i, args[5].i, args[6].i, col);
    return FX_OK;
}

static int ng_drawArc(vm *v, jthread *t, jclass *oc,
                      const jslot *args, jslot *ret)
{
    fx_surface s;
    int tx, ty, col;
    (void)t; (void)oc; (void)ret;
    if (gfx_begin(v, args[0].r, &s, &tx, &ty, &col) != FX_OK)
        return FX_OK;
    fx_draw_arc(&s, args[1].i + tx, args[2].i + ty, args[3].i,
                args[4].i, args[5].i, args[6].i, col);
    return FX_OK;
}

static int ng_fillArc(vm *v, jthread *t, jclass *oc,
                      const jslot *args, jslot *ret)
{
    fx_surface s;
    int tx, ty, col;
    (void)t; (void)oc; (void)ret;
    if (gfx_begin(v, args[0].r, &s, &tx, &ty, &col) != FX_OK)
        return FX_OK;
    fx_fill_arc(&s, args[1].i + tx, args[2].i + ty, args[3].i,
                args[4].i, args[5].i, args[6].i, col);
    return FX_OK;
}

static int ng_fillTriangle(vm *v, jthread *t, jclass *oc,
                           const jslot *args, jslot *ret)
{
    fx_surface s;
    int tx, ty, col;
    (void)t; (void)oc; (void)ret;
    if (gfx_begin(v, args[0].r, &s, &tx, &ty, &col) != FX_OK)
        return FX_OK;
    fx_fill_triangle(&s, args[1].i + tx, args[2].i + ty,
                     args[3].i + tx, args[4].i + ty,
                     args[5].i + tx, args[6].i + ty, col);
    return FX_OK;
}

/* Anchor adjustment shared by text/char drawing. Returns the cell y. */
static int anchor_y(int y, int anchor)
{
    if (anchor & 16)                  /* TOP */
        return y;
    if (anchor & 32)                  /* BOTTOM */
        return y - FX_FONT_HEIGHT;
    if (anchor & 2)                   /* VCENTER */
        return y - FX_FONT_HEIGHT / 2;
    return y - FX_FONT_ASCENT;        /* default: y is the baseline */
}

static int anchor_x_text(int x, int anchor, int width)
{
    if (anchor & 1)                   /* HCENTER */
        return x - width / 2;
    if (anchor & 8)                   /* RIGHT */
        return x - width;
    return x;                         /* LEFT / default */
}

/* Draw a UTF-16 range with MIDP anchor semantics. */
static int draw_text(vm *v, jref g, const uint16_t *chars, int off, int len,
                     int x, int y, int anchor)
{
    fx_surface s;
    int tx, ty, col;
    int px, py;

    if (gfx_begin(v, g, &s, &tx, &ty, &col) != FX_OK)
        return FX_OK;
    px = anchor_x_text(x, anchor, len * FX_FONT_ADVANCE) + tx;
    py = anchor_y(y, anchor) + ty;
    fx_draw_string(&s, px, py, chars + off, len, col);
    return FX_OK;
}

static int ng_drawString(vm *v, jthread *t, jclass *oc,
                         const jslot *args, jslot *ret)
{
    const uint16_t *chars;
    int len, r;
    (void)oc; (void)ret;
    if (args[1].r == JREF_NULL)
        return vm_throw(v, t, "java/lang/NullPointerException", "drawString");
    r = vm_str_chars(v, args[1].r, &chars, &len);
    if (r != FX_OK)
        return r;
    return draw_text(v, args[0].r, chars, 0, len,
                     args[2].i, args[3].i, args[4].i);
}

static int ng_drawSubstring(vm *v, jthread *t, jclass *oc,
                            const jslot *args, jslot *ret)
{
    const uint16_t *chars;
    int len, r;
    int32_t off = args[2].i, n = args[3].i;
    (void)oc; (void)ret;
    if (args[1].r == JREF_NULL)
        return vm_throw(v, t, "java/lang/NullPointerException",
                        "drawSubstring");
    r = vm_str_chars(v, args[1].r, &chars, &len);
    if (r != FX_OK)
        return r;
    if (off < 0 || n < 0 || off + n > len)
        return vm_throw(v, t,
                        "java/lang/StringIndexOutOfBoundsException",
                        "drawSubstring");
    return draw_text(v, args[0].r, chars, off, n,
                     args[4].i, args[5].i, args[6].i);
}

static int ng_drawChar(vm *v, jthread *t, jclass *oc,
                       const jslot *args, jslot *ret)
{
    uint16_t ch = (uint16_t)args[1].i;
    (void)oc; (void)t; (void)ret;
    return draw_text(v, args[0].r, &ch, 0, 1,
                     args[2].i, args[3].i, args[4].i);
}

/* Anchor adjustment for images/regions (size w x h). */
static void anchor_xy(int x, int y, int anchor, int w, int h,
                      int *ox, int *oy)
{
    *ox = x;
    *oy = y;
    if (anchor & 1) *ox -= w / 2;
    else if (anchor & 8) *ox -= w;
    if (anchor & 2) *oy -= h / 2;
    else if (anchor & 32) *oy -= h;
}

static int ng_drawImage(vm *v, jthread *t, jclass *oc,
                        const jslot *args, jslot *ret)
{
    fx_surface dst, src;
    int tx, ty, col, w, h, dx, dy;
    (void)t; (void)oc; (void)ret;
    if (gfx_begin(v, args[0].r, &dst, &tx, &ty, &col) != FX_OK)
        return FX_OK;
    (void)col;
    if (img_surface(v, args[1].r, &src) != FX_OK)
        return FX_OK;
    w = src.w;
    h = src.h;
    anchor_xy(args[2].i, args[3].i, args[4].i, w, h, &dx, &dy);
    fx_blit(&dst, dx + tx, dy + ty, &src, 0, 0, w, h, 0);
    return FX_OK;
}

static int ng_drawRegion(vm *v, jthread *t, jclass *oc,
                         const jslot *args, jslot *ret)
{
    fx_surface dst, src;
    int tx, ty, col, tr, w, h, dx, dy, i, j;
    int32_t xs = args[2].i, ys = args[3].i;
    (void)t; (void)oc; (void)ret;
    tr = args[6].i;
    if (tr < 0 || tr > 7)
        return vm_throw(v, t, "java/lang/IllegalArgumentException",
                        "drawRegion transform");
    if (gfx_begin(v, args[0].r, &dst, &tx, &ty, &col) != FX_OK)
        return FX_OK;
    (void)col;
    if (img_surface(v, args[1].r, &src) != FX_OK)
        return FX_OK;
    w = args[4].i;
    h = args[5].i;
    if (w <= 0 || h <= 0)
        return FX_OK;
    {
        int tw = (tr >= 4) ? h : w;
        int th = (tr >= 4) ? w : h;
        anchor_xy(args[7].i, args[8].i, args[9].i, tw, th, &dx, &dy);
    }
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            int p = fx_pixel_get(&src, xs + i, ys + j);
            int di = i, dj = j;
            switch (tr) {
            case 0: di = i; dj = j; break;
            case 1: di = i; dj = h - 1 - j; break;
            case 2: di = w - 1 - i; dj = j; break;
            case 3: di = w - 1 - i; dj = h - 1 - j; break;
            case 4: di = h - 1 - j; dj = w - 1 - i; break;
            case 5: di = h - 1 - j; dj = i; break;
            case 6: di = j; dj = i; break;
            case 7: di = j; dj = w - 1 - i; break;
            }
            fx_pixel(&dst, dx + di + tx, dy + dj + ty, p);
        }
    }
    return FX_OK;
}

static int ng_drawRGB(vm *v, jthread *t, jclass *oc,
                      const jslot *args, jslot *ret)
{
    fx_surface s;
    int tx, ty, col;
    jref arr = args[1].r;
    int32_t off = args[2].i, scan = args[3].i;
    int32_t x = args[4].i, y = args[5].i, w = args[6].i, h = args[7].i;
    int alpha = args[8].i;
    jblk *b;
    const int32_t *rgb;
    int i, j;
    (void)t; (void)oc; (void)ret;

    if (gfx_begin(v, args[0].r, &s, &tx, &ty, &col) != FX_OK)
        return FX_OK;
    (void)col;
    if (arr == JREF_NULL)
        return vm_throw(v, t, "java/lang/NullPointerException", "drawRGB");
    b = heap_hdr(&v->heap, arr);
    rgb = (const int32_t *)heap_ptr(&v->heap, arr);

    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            int32_t idx = off + (int32_t)j * scan + i;
            uint32_t c;
            int rr, gg, bb, aa, lum, black;
            if (idx < 0 || (uint32_t)idx >= b->aux)
                continue;
            c = (uint32_t)rgb[idx];
            aa = (int)((c >> 24) & 0xff);
            if (alpha && aa < 128)
                continue;                    /* transparent pixel */
            rr = (int)((c >> 16) & 0xff);
            gg = (int)((c >> 8) & 0xff);
            bb = (int)(c & 0xff);
            lum = (rr * 30 + gg * 59 + bb * 11) / 100;
            black = lum < png_bayer[(y + j) & 3][(x + i) & 3] * 16 + 8;
            fx_pixel(&s, x + i + tx, y + j + ty, black);
        }
    }
    return FX_OK;
}

/*--- Image natives -----------------------------------------------------*/

static int ng_getRGB(vm *v, jthread *t, jclass *oc,
                     const jslot *args, jslot *ret)
{
    fx_surface s;
    jref arr = args[1].r;
    int32_t off = args[2].i, scan = args[3].i;
    int32_t x = args[4].i, y = args[5].i, w = args[6].i, h = args[7].i;
    jblk *b;
    int32_t *rgb;
    int i, j;
    (void)t; (void)oc; (void)ret;

    if (img_surface(v, args[0].r, &s) != FX_OK)
        return FX_OK;
    if (arr == JREF_NULL)
        return vm_throw(v, t, "java/lang/NullPointerException", "getRGB");
    b = heap_hdr(&v->heap, arr);
    rgb = (int32_t *)heap_ptr(&v->heap, arr);

    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            int32_t idx = off + (int32_t)j * scan + i;
            int px = x + i, py = y + j, v;
            if (idx < 0 || (uint32_t)idx >= b->aux)
                continue;
            if (px < 0 || py < 0 || px >= s.w || py >= s.h) {
                rgb[idx] = 0;               /* outside: transparent black */
                continue;
            }
            v = fx_pixel_get(&s, px, py);
            rgb[idx] = v ? (int32_t)0xff000000 : (int32_t)0xffffffff;
        }
    }
    return FX_OK;
}

static int ng_decodePNG(vm *v, jthread *t, jclass *oc,
                        const jslot *args, jslot *ret)
{
    jref data = args[0].r;
    int32_t off = args[1].i, len = args[2].i;
    jblk *b;
    uint8_t *bits = NULL;
    int w = 0, h = 0, r;
    (void)oc;

    ret[0].r = JREF_NULL;
    ret[1].i = 0;
    if (data == JREF_NULL)
        return vm_throw(v, t, "java/lang/NullPointerException", "decodePNG");
    b = heap_hdr(&v->heap, data);
    if (off < 0 || len < 0 || (uint32_t)(off + len) > b->aux)
        return vm_throw(v, t, "java/lang/IllegalArgumentException",
                        "decodePNG range");
    r = png_decode((const uint8_t *)heap_ptr(&v->heap, data) + off,
                   (size_t)len, &bits, &w, &h);
    if (r != FX_OK)
        return vm_throw(v, t, "java/lang/IllegalArgumentException",
                        "unsupported PNG");
    ret[0].r = make_image(v, w, h, bits);
    free(bits);
    if (ret[0].r == JREF_NULL)
        return vm_throw(v, t, "java/lang/OutOfMemoryError", "decodePNG");
    return FX_OK;
}

/*--- registry ----------------------------------------------------------*/

static const jnat_entry midp_table[] = {
    { "javax/microedition/lcdui/Graphics", "drawLine", "(IIII)V",
      ng_drawLine },
    { "javax/microedition/lcdui/Graphics", "drawRect", "(IIII)V",
      ng_drawRect },
    { "javax/microedition/lcdui/Graphics", "fillRect", "(IIII)V",
      ng_fillRect },
    { "javax/microedition/lcdui/Graphics", "drawRoundRect", "(IIIIII)V",
      ng_drawRoundRect },
    { "javax/microedition/lcdui/Graphics", "fillRoundRect", "(IIIIII)V",
      ng_fillRoundRect },
    { "javax/microedition/lcdui/Graphics", "drawArc", "(IIIIII)V",
      ng_drawArc },
    { "javax/microedition/lcdui/Graphics", "fillArc", "(IIIIII)V",
      ng_fillArc },
    { "javax/microedition/lcdui/Graphics", "fillTriangle", "(IIIIII)V",
      ng_fillTriangle },
    { "javax/microedition/lcdui/Graphics", "drawString",
      "(Ljava/lang/String;III)V", ng_drawString },
    { "javax/microedition/lcdui/Graphics", "drawSubstring",
      "(Ljava/lang/String;IIIII)V", ng_drawSubstring },
    { "javax/microedition/lcdui/Graphics", "drawChar", "(CIII)V",
      ng_drawChar },
    { "javax/microedition/lcdui/Graphics", "drawImage",
      "(Ljavax/microedition/lcdui/Image;III)V", ng_drawImage },
    { "javax/microedition/lcdui/Graphics", "drawRegion",
      "(Ljavax/microedition/lcdui/Image;IIIIIIII)V", ng_drawRegion },
    { "javax/microedition/lcdui/Graphics", "drawRGB", "([IIIIIIIZ)V",
      ng_drawRGB },
    { "javax/microedition/lcdui/Image", "getRGB", "([IIIIIII)V",
      ng_getRGB },
    { "javax/microedition/lcdui/Image", "decodePNG",
      "([BII)Ljavax/microedition/lcdui/Image;", ng_decodePNG },
};

static void register_natives(void)
{
    static int done;
    if (!done) {
        natives_add(midp_table,
                    (int)(sizeof midp_table / sizeof midp_table[0]));
        done = 1;
    }
}

/*--- launcher ----------------------------------------------------------*/

static int startup_fail(vm *v, jthread *t, const char *msg)
{
    if (msg)
        snprintf(st.err, sizeof st.err, "%s", msg);
    else if (t->exception != JREF_NULL)
        vm_exc_text(v, t, st.err, sizeof st.err);
    else
        snprintf(st.err, sizeof st.err, "%s",
                 t->error ? t->error : "startup failed");
    st.finished = 1;
    return FX_ERR_JAVA;
}

static int app_fail(void)
{
    if (st.t->exception != JREF_NULL)
        vm_exc_text(st.v, st.t, st.err, sizeof st.err);
    else
        snprintf(st.err, sizeof st.err, "%s",
                 st.t->error ? st.t->error : "application error");
    st.finished = 1;
    return FX_ERR_JAVA;
}

/* Manifest attributes -> two String[] arrays for MIDlet.getAppProperty. */
static int build_props(vm *v, const midlet_info *info, jref *keys_out,
                       jref *vals_out)
{
    jheap *hp = &v->heap;
    jref ka, va;
    size_t used = 0;
    int i, n = info->nattrs;

    *keys_out = JREF_NULL;
    *vals_out = JREF_NULL;
    if (n <= 0)
        return FX_OK;

    ka = vm_alloc(v, (size_t)n * 4, NULL, JBLK_ARRAY | JBLK_REFELEM,
                  (uint32_t)n);
    if (ka == JREF_NULL)
        return FX_ERR_NOSPACE;
    st.scratch[0] = ka;
    va = vm_alloc(v, (size_t)n * 4, NULL, JBLK_ARRAY | JBLK_REFELEM,
                  (uint32_t)n);
    if (va == JREF_NULL) {
        st.scratch[0] = JREF_NULL;
        return FX_ERR_NOSPACE;
    }
    st.scratch[1] = va;

    for (i = 0; i < n; i++) {
        const char *e = info->attrs + used;
        const char *eq = strchr(e, '=');
        size_t klen = eq ? (size_t)(eq - e) : strlen(e);
        const char *val = eq ? eq + 1 : e + klen;
        jref k, valref;

        st.scratch[2] = ka;
        st.scratch[3] = va;
        k = vm_make_string(v, (const uint8_t *)e, (int)klen);
        st.scratch[2] = k;
        valref = vm_make_string(v, (const uint8_t *)val, (int)strlen(val));
        ((jref *)heap_ptr(hp, ka))[i] = k;
        ((jref *)heap_ptr(hp, va))[i] = valref;
        st.scratch[2] = JREF_NULL;
        st.scratch[3] = JREF_NULL;

        used += strlen(e) + 1;
    }
    st.scratch[0] = JREF_NULL;
    st.scratch[1] = JREF_NULL;
    *keys_out = ka;
    *vals_out = va;
    return FX_OK;
}

int midp_start(vm *v, jthread *t, const midlet_info *info)
{
    jslot args[8], ret[2];
    jclass *mc;
    jref keys = JREF_NULL, vals = JREF_NULL;
    int r;

    memset(&st, 0, sizeof st);
    st.v = v;
    st.t = t;
    register_natives();
    ensure_roots(v);

    /* the screen: a mutable 128x64 Image */
    args[0].i = MIDP_FRAME_W;
    args[1].i = MIDP_FRAME_H;
    r = m_static(v, t, "javax/microedition/lcdui/Image", "createImage",
                 "(II)Ljavax/microedition/lcdui/Image;", args, ret);
    if (r != FX_OK)
        return startup_fail(v, t, NULL);
    if (ret[0].r == JREF_NULL)
        return startup_fail(v, t, "screen allocation failed");
    st.screen = ret[0].r;

    /* the MIDlet instance */
    mc = vm_load_class(v, info->cls);
    if (!mc) {
        /* vm_load_class names the failing class (often a missing
         * superclass); prefer that over a generic message. */
        if (v->errbuf[0])
            snprintf(st.err, sizeof st.err, "%.127s", v->errbuf);
        else
            snprintf(st.err, sizeof st.err, "MIDlet class not found: %.80s",
                     info->cls);
        st.finished = 1;
        return FX_ERR_JAVA;
    }
    st.midlet = vm_alloc(v, (size_t)mc->nslots * sizeof(jslot), mc, 0, 0);
    if (st.midlet == JREF_NULL)
        return startup_fail(v, t, "out of memory (MIDlet)");

    /* manifest properties before <init>: available in the constructor */
    if (build_props(v, info, &keys, &vals) == FX_OK) {
        set_ref(v, st.midlet, "propKeys", "[Ljava/lang/String;", keys);
        set_ref(v, st.midlet, "propValues", "[Ljava/lang/String;", vals);
    }

    r = vm_call(v, t, mc, "<init>", "()V", st.midlet, NULL, ret);
    if (r != FX_OK)
        return startup_fail(v, t, NULL);
    r = m_call(v, t, st.midlet, "callStartApp", "()V", NULL, NULL);
    if (r != FX_OK)
        return startup_fail(v, t, NULL);
    return FX_OK;
}

/*--- pump --------------------------------------------------------------*/

static void clear_frame(vm *v)
{
    jref pix = fld_ref(v, st.screen, "pix", "[B");
    if (pix != JREF_NULL)
        memset(heap_ptr(&v->heap, pix), 0, MIDP_FRAME_SIZE);
}

static int do_paint(void)
{
    vm *v = st.v;
    jthread *t = st.t;
    jslot args[2], ret[2];
    int r;

    r = m_call(v, t, st.screen, "getGraphics",
               "()Ljavax/microedition/lcdui/Graphics;", NULL, ret);
    if (r != FX_OK)
        return r;
    st.scratch[0] = ret[0].r;

    r = m_static(v, t, "javax/microedition/lcdui/Display", "getCurrent",
                 "()Ljavax/microedition/lcdui/Displayable;", NULL, ret);
    if (r != FX_OK) {
        st.scratch[0] = JREF_NULL;
        return r;
    }
    st.scratch[1] = ret[0].r;

    if (ret[0].r != JREF_NULL) {
        if (ret[0].r != st.last_canvas) {
            clear_frame(v);                /* fresh screen: start white */
            st.last_canvas = ret[0].r;
        }
        args[0].r = st.scratch[0];
        r = m_call(v, t, ret[0].r, "callPaint",
                   "(Ljavax/microedition/lcdui/Graphics;)V", args, NULL);
    }
    st.scratch[0] = JREF_NULL;
    st.scratch[1] = JREF_NULL;
    return r;
}

int midp_poll(void)
{
    vm *v = st.v;
    jthread *t = st.t;
    jslot args[2], ret[2];
    int r;

    if (!st.midlet)
        return FX_ERR_NOTFOUND;
    if (st.finished)
        return FX_OK;

    /* a pending callSerially Runnable */
    r = m_static(v, t, "javax/microedition/lcdui/Display", "takeRunnable",
                 "()Ljava/lang/Runnable;", NULL, ret);
    if (r != FX_OK)
        return app_fail();
    if (ret[0].r != JREF_NULL) {
        st.scratch[0] = ret[0].r;
        r = m_call(v, t, ret[0].r, "run", "()V", NULL, NULL);
        st.scratch[0] = JREF_NULL;
        if (r != FX_OK)
            return app_fail();
    }

    /* repaint if requested */
    r = m_static(v, t, "javax/microedition/lcdui/Display",
                 "takeRepaintRequest", "()Z", NULL, ret);
    if (r != FX_OK)
        return app_fail();
    if (ret[0].i) {
        r = do_paint();
        if (r != FX_OK)
            return app_fail();
    }

    /* lifecycle: notifyDestroyed() -> destroyApp(true) */
    r = m_call(v, t, st.midlet, "isDestroyed", "()Z", NULL, ret);
    if (r != FX_OK)
        return app_fail();
    if (ret[0].i) {
        args[0].i = 1;
        r = m_call(v, t, st.midlet, "callDestroyApp", "(Z)V", args, NULL);
        st.finished = 1;
        if (r != FX_OK)
            return app_fail();
    }
    return FX_OK;
}

int midp_key(int code, int kind)
{
    vm *v = st.v;
    jthread *t = st.t;
    jslot args[2], ret[2];
    const char *m;
    jref d;
    int r;

    if (!st.midlet)
        return FX_ERR_NOTFOUND;
    if (st.finished)
        return FX_OK;

    r = m_static(v, t, "javax/microedition/lcdui/Display", "getCurrent",
                 "()Ljavax/microedition/lcdui/Displayable;", NULL, ret);
    if (r != FX_OK)
        return app_fail();
    d = ret[0].r;
    if (d == JREF_NULL)
        return FX_OK;

    m = kind == MIDP_KEY_DOWN ? "callKeyPressed" :
        kind == MIDP_KEY_REPEAT ? "callKeyRepeated" : "callKeyReleased";
    st.scratch[0] = d;
    args[0].i = code;
    r = m_call(v, t, d, m, "(I)V", args, NULL);
    st.scratch[0] = JREF_NULL;
    return r == FX_OK ? FX_OK : app_fail();
}

int midp_softkey(int idx)
{
    vm *v = st.v;
    jthread *t = st.t;
    jslot args[2], ret[2];
    jref d, c;
    int r;

    if (!st.midlet)
        return FX_ERR_NOTFOUND;
    if (st.finished)
        return FX_OK;

    r = m_static(v, t, "javax/microedition/lcdui/Display", "getCurrent",
                 "()Ljavax/microedition/lcdui/Displayable;", NULL, ret);
    if (r != FX_OK)
        return app_fail();
    d = ret[0].r;
    if (d == JREF_NULL)
        return FX_OK;

    st.scratch[0] = d;
    r = m_call(v, t, d, "commandCount", "()I", NULL, ret);
    if (r == FX_OK && idx >= 0 && idx < ret[0].i) {
        args[0].i = idx;
        r = m_call(v, t, d, "commandAt",
                   "(I)Ljavax/microedition/lcdui/Command;", args, ret);
        c = ret[0].r;
        if (r == FX_OK && c != JREF_NULL) {
            st.scratch[1] = c;
            args[0].r = c;
            r = m_call(v, t, d, "fireCommand",
                       "(Ljavax/microedition/lcdui/Command;)V", args, NULL);
            st.scratch[1] = JREF_NULL;
        }
    }
    st.scratch[0] = JREF_NULL;
    return r == FX_OK ? FX_OK : app_fail();
}

const uint8_t *midp_frame(void)
{
    jref pix;
    if (!st.screen)
        return NULL;
    pix = fld_ref(st.v, st.screen, "pix", "[B");
    if (pix == JREF_NULL)
        return NULL;
    return (const uint8_t *)heap_ptr(&st.v->heap, pix);
}

int midp_finished(void)
{
    return st.finished;
}

void midp_stop(void)
{
    if (st.v && st.t && st.midlet != JREF_NULL && !st.finished) {
        jslot args[2];
        args[0].i = 1;
        m_call(st.v, st.t, st.midlet, "callDestroyApp", "(Z)V", args, NULL);
        st.finished = 1;
    }
    st.screen = JREF_NULL;
    st.midlet = JREF_NULL;
    st.last_canvas = JREF_NULL;
}

const char *midp_error(void)
{
    return st.err;
}
