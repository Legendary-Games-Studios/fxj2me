/* fxj2me: clean-room DEFLATE decompressor (RFC 1951), puff-style canonical
 * Huffman decoding. No allocation, no dependencies. */
#include "inflate.h"
#include <string.h>

#define MAXLCODES 286   /* literal/length codes (288 in the fixed table)   */
#define MAXDCODES 30    /* distance codes                                  */
#define MAXCODES  (MAXLCODES + MAXDCODES)
#define FIXLCODES 288

struct huff {
    int16_t count[16];   /* number of codes of each length  */
    int16_t symbol[MAXCODES]; /* symbols ordered by code   */
};

/* --- bit reader (LSB-first) ------------------------------------------- */

static int bits(inf_ctx *c, int need)
{
    uint32_t val = c->bitbuf;
    while (c->bitcnt < need) {
        if (c->srcpos >= c->srclen)
            return -1;
        val |= (uint32_t)c->src[c->srcpos++] << c->bitcnt;
        c->bitcnt += 8;
    }
    c->bitbuf = val >> need;
    c->bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
}

/* --- canonical Huffman construction ----------------------------------- */

static int construct(struct huff *h, const int16_t *length, int n)
{
    int sym, len, left;
    int16_t offs[16];

    for (len = 0; len < 16; len++)
        h->count[len] = 0;
    for (sym = 0; sym < n; sym++)
        h->count[length[sym]]++;
    if (h->count[0] == n)
        return 0;                    /* no codes at all: legal */

    left = 1;
    for (len = 1; len < 16; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
            return -1;               /* over-subscribed */
    }

    offs[1] = 0;
    for (len = 1; len < 15; len++)
        offs[len + 1] = (int16_t)(offs[len] + h->count[len]);
    for (sym = 0; sym < n; sym++)
        if (length[sym])
            h->symbol[offs[length[sym]]++] = (int16_t)sym;

    return left;                     /* 0 = complete, >0 = incomplete */
}

static int decode(inf_ctx *c, const struct huff *h)
{
    int code = 0, first = 0, index = 0, len;

    for (len = 1; len <= 16; len++) {
        int b = bits(c, 1);
        int count;
        if (b < 0)
            return -1;
        code |= b;
        count = h->count[len];
        if (code - first < count)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

/* --- block tables ----------------------------------------------------- */

static const int16_t lbase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51,
    59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const int16_t lextra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4,
    4, 5, 5, 5, 5, 0
};
static const int16_t dbase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385,
    513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const int16_t dextra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10,
    10, 11, 11, 12, 12, 13, 13
};

static void fixed_tables(struct huff *lencode, struct huff *distcode)
{
    int16_t lengths[FIXLCODES];
    int i;

    for (i = 0; i < 144; i++) lengths[i] = 8;
    for (; i < 256; i++)        lengths[i] = 9;
    for (; i < 280; i++)        lengths[i] = 7;
    for (; i < FIXLCODES; i++)  lengths[i] = 8;
    construct(lencode, lengths, FIXLCODES);

    for (i = 0; i < 30; i++) lengths[i] = 5;
    construct(distcode, lengths, 30);
}

/* --- output ----------------------------------------------------------- */

static int putbyte(inf_ctx *c, uint8_t b)
{
    if (c->dstpos >= c->dstcap)
        return -1;
    c->dst[c->dstpos++] = b;
    return 0;
}

/* --- blocks ----------------------------------------------------------- */

static int stored_block(inf_ctx *c)
{
    unsigned len;

    c->bitbuf = 0;
    c->bitcnt = 0;                       /* align to byte boundary */
    if (c->srcpos + 4 > c->srclen)
        return -1;
    len = (unsigned)c->src[c->srcpos] | ((unsigned)c->src[c->srcpos + 1] << 8);
    if (len != (0xffffu ^ ((unsigned)c->src[c->srcpos + 2] |
                           ((unsigned)c->src[c->srcpos + 3] << 8))))
        return -1;                       /* nlen must be ~len */
    c->srcpos += 4;
    if (c->srcpos + len > c->srclen || c->dstpos + len > c->dstcap)
        return -1;
    memcpy(c->dst + c->dstpos, c->src + c->srcpos, len);
    c->srcpos += len;
    c->dstpos += len;
    return 0;
}

static int dynamic_tables(inf_ctx *c, struct huff *lencode, struct huff *distcode)
{
    static const int16_t order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    int16_t lengths[MAXCODES];
    int16_t clens[19];
    struct huff clencode;
    int nlen, ndist, ncode, index, err;

    nlen = bits(c, 5) + 257;
    ndist = bits(c, 5) + 1;
    ncode = bits(c, 4) + 4;
    if (nlen < 0 || ndist < 0 || ncode < 0)
        return -1;
    if (nlen > 286 || ndist > 30)
        return -1;

    memset(clens, 0, sizeof clens);
    for (index = 0; index < ncode; index++) {
        int v = bits(c, 3);
        if (v < 0)
            return -1;
        clens[order[index]] = (int16_t)v;
    }
    err = construct(&clencode, clens, 19);
    if (err != 0)
        return -1;

    index = 0;
    while (index < nlen + ndist) {
        int sym = decode(c, &clencode);
        int rep, value = 0;
        if (sym < 0)
            return -1;
        if (sym < 16) {
            lengths[index++] = (int16_t)sym;
            continue;
        }
        if (sym == 16) {
            if (index == 0)
                return -1;
            value = lengths[index - 1];
            rep = 3 + bits(c, 2);
        } else if (sym == 17) {
            rep = 3 + bits(c, 3);
        } else {
            rep = 11 + bits(c, 7);
        }
        if (rep < 0 || index + rep > nlen + ndist)
            return -1;
        while (rep--)
            lengths[index++] = (int16_t)value;
    }
    if (lengths[256] == 0)
        return -1;                       /* must have an end-of-block code */

    err = construct(lencode, lengths, nlen);
    if (err != 0)
        return -1;
    err = construct(distcode, lengths + nlen, ndist);
    if (err != 0)
        return -1;
    return 0;
}

static int compressed_block(inf_ctx *c, const struct huff *lencode,
                            const struct huff *distcode)
{
    for (;;) {
        int sym = decode(c, lencode);
        if (sym < 0)
            return -1;
        if (sym == 256)
            return 0;
        if (sym < 256) {
            if (putbyte(c, (uint8_t)sym) < 0)
                return -1;
        } else {
            unsigned len, dist, i;

            sym -= 257;
            if (sym >= 29)
                return -1;
            {
                int extra = bits(c, lextra[sym]);
                if (extra < 0)
                    return -1;
                len = (unsigned)lbase[sym] + (unsigned)extra;
            }
            {
                int dsym = decode(c, distcode);
                int extra;
                if (dsym < 0 || dsym >= 30)
                    return -1;
                extra = bits(c, dextra[dsym]);
                if (extra < 0)
                    return -1;
                dist = (unsigned)dbase[dsym] + (unsigned)extra;
            }
            if (dist > c->dstpos || len > c->dstcap - c->dstpos)
                return -1;
            for (i = 0; i < len; i++) {
                if (putbyte(c, c->dst[c->dstpos - dist]) < 0)
                    return -1;
            }
        }
    }
}

/* --- driver ----------------------------------------------------------- */

void inf_init(inf_ctx *c, const void *src, size_t srclen,
              void *dst, size_t dstcap)
{
    c->src = (const uint8_t *)src;
    c->srclen = srclen;
    c->srcpos = 0;
    c->dst = (uint8_t *)dst;
    c->dstcap = dstcap;
    c->dstpos = 0;
    c->bitbuf = 0;
    c->bitcnt = 0;
}

int inf_run(inf_ctx *c)
{
    for (;;) {
        int final = bits(c, 1);
        int type = bits(c, 2);
        struct huff lencode, distcode;
        int err;

        if (final < 0 || type < 0)
            return FX_ERR_FORMAT;

        if (type == 0) {
            err = stored_block(c);
        } else if (type == 1) {
            fixed_tables(&lencode, &distcode);
            err = compressed_block(c, &lencode, &distcode);
        } else if (type == 2) {
            err = dynamic_tables(c, &lencode, &distcode);
            if (err == 0)
                err = compressed_block(c, &lencode, &distcode);
        } else {
            err = FX_ERR_FORMAT;
        }
        if (err != 0)
            return err < 0 && err != FX_ERR_FORMAT ? err : FX_ERR_FORMAT;
        if (final)
            return FX_OK;
    }
}
