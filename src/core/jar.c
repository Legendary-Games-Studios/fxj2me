/* fxj2me: minimal ZIP reader for .jar files (memory-backed). */
#include "jar.h"
#include "inflate.h"
#include <stdlib.h>
#include <string.h>

#define SIG_EOCD 0x06054b50u
#define SIG_CD   0x02014b50u
#define SIG_LH   0x04034b50u

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* CRC-32 (IEEE) over extracted bytes, nibble table: 64 B of flash, not 1 kB. */
static uint32_t crc32_bytes(const uint8_t *p, size_t n)
{
    static const uint32_t tbl[16] = {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
        0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
        0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
        0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
    };
    uint32_t crc = 0xffffffffu;
    while (n--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ tbl[crc & 15];
        crc = (crc >> 4) ^ tbl[crc & 15];
    }
    return crc ^ 0xffffffffu;
}
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int jar_open(jar *j, const void *blob, size_t len)
{
    const uint8_t *b = (const uint8_t *)blob;
    size_t i, cd_off, cd_size, pos;
    uint16_t nentries;

    memset(j, 0, sizeof *j);
    if (len < 22)
        return FX_ERR_FORMAT;

    /* Locate the end-of-central-directory record (bounded 64K scan back). */
    {
        size_t min_i = (len - 22 > 65536) ? len - 22 - 65536 : 0;
        for (i = len - 22; ; i--) {
            if (rd32(b + i) == SIG_EOCD)
                break;
            if (i == min_i)
                return FX_ERR_FORMAT;
        }
    }

    nentries = rd16(b + i + 10);
    cd_size = rd32(b + i + 12);
    cd_off = rd32(b + i + 16);
    if (cd_off >= len || cd_size > len - cd_off || nentries == 0xffff)
        return FX_ERR_FORMAT;

    j->blob = b;
    j->len = len;
    j->nentries = nentries;
    j->entries = calloc(nentries ? nentries : 1, sizeof(zip_entry));
    if (!j->entries)
        return FX_ERR_NOSPACE;

    pos = cd_off;
    for (i = 0; i < (size_t)nentries; i++) {
        zip_entry *e = &j->entries[i];
        uint16_t nlen, elen, cmtlen;
        char *name;

        if (pos + 46 > len || rd32(b + pos) != SIG_CD)
            goto fail;
        e->method = rd16(b + pos + 10);
        e->crc = rd32(b + pos + 16);
        e->csize = rd32(b + pos + 20);
        e->usize = rd32(b + pos + 24);
        nlen = rd16(b + pos + 28);
        elen = rd16(b + pos + 30);
        cmtlen = rd16(b + pos + 32);
        e->data_off = rd32(b + pos + 42);
        if (pos + 46 + nlen > len)
            goto fail;

        name = malloc((size_t)nlen + 1);
        if (!name)
            goto fail;
        memcpy(name, b + pos + 46, nlen);
        name[nlen] = 0;
        e->name = name;

        pos += 46 + (size_t)nlen + elen + cmtlen;
    }
    return FX_OK;

fail:
    jar_close(j);
    return FX_ERR_FORMAT;
}

void jar_close(jar *j)
{
    int i;
    if (j->entries) {
        for (i = 0; i < j->nentries; i++)
            free((void *)j->entries[i].name);
        free(j->entries);
    }
    memset(j, 0, sizeof *j);
}

int jar_find(const jar *j, const char *name)
{
    int i;
    for (i = 0; i < j->nentries; i++)
        if (strcmp(j->entries[i].name, name) == 0)
            return i;
    return FX_ERR_NOTFOUND;
}

int jar_extract(const jar *j, int idx, void *dst, size_t cap, size_t *outlen)
{
    const zip_entry *e;
    const uint8_t *p;
    uint16_t nlen, elen;
    size_t off;

    if (idx < 0 || idx >= j->nentries)
        return FX_ERR_NOTFOUND;
    e = &j->entries[idx];
    if (e->usize > cap)
        return FX_ERR_NOSPACE;

    /* Data starts after the local file header + its name/extra fields. */
    p = j->blob + e->data_off;
    if (e->data_off + 30 > j->len || rd32(p) != SIG_LH)
        return FX_ERR_FORMAT;
    nlen = rd16(p + 26);
    elen = rd16(p + 28);
    off = (size_t)e->data_off + 30 + nlen + elen;
    if (off + e->csize > j->len)
        return FX_ERR_FORMAT;

    if (e->method == 0) {
        memcpy(dst, j->blob + off, e->usize);
    } else if (e->method == 8) {
        inf_ctx c;
        inf_init(&c, j->blob + off, e->csize, dst, cap);
        if (inf_run(&c) != FX_OK || c.dstpos != e->usize)
            return FX_ERR_FORMAT;
    } else {
        return FX_ERR_UNIMPL;
    }
    /* Catch damaged copies (bad USB transfers) at the source. */
    if (crc32_bytes((const uint8_t *)dst, e->usize) != e->crc)
        return FX_ERR_FORMAT;
    if (outlen)
        *outlen = e->usize;
    return FX_OK;
}
