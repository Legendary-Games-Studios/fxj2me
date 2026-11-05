/* fxj2me: raw DEFLATE (RFC 1951) decompressor. ZIP "method 8" data is raw
 * deflate with no zlib wrapper, which is what .jar entries use. */
#ifndef FXJ2ME_INFLATE_H
#define FXJ2ME_INFLATE_H

#include "fxj2me.h"

typedef struct inf_ctx {
    const uint8_t *src;
    size_t srclen, srcpos;
    uint8_t *dst;
    size_t dstcap, dstpos;
    uint32_t bitbuf;
    int bitcnt;
} inf_ctx;

void inf_init(inf_ctx *c, const void *src, size_t srclen,
              void *dst, size_t dstcap);

/* Inflate one deflate stream. Returns FX_OK, or a negative status on error.
 * On success c->dstpos is the number of bytes produced. */
int inf_run(inf_ctx *c);

#endif /* FXJ2ME_INFLATE_H */
