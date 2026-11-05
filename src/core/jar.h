/* fxj2me: minimal ZIP reader for .jar files (memory-backed). */
#ifndef FXJ2ME_JAR_H
#define FXJ2ME_JAR_H

#include "fxj2me.h"

/* zip_entry: one archived file. Named to not collide with the shell's
 * jar_entry (scan.h), which is a UI list item. */
typedef struct zip_entry {
    const char *name;      /* NUL-terminated copy               */
    uint16_t method;       /* 0 = stored, 8 = deflate           */
    uint32_t crc;          /* CRC-32 of the uncompressed data   */
    uint32_t csize;        /* compressed size                   */
    uint32_t usize;        /* uncompressed size                 */
    uint32_t data_off;     /* offset of entry data in the blob  */
} zip_entry;

typedef struct jar {
    const uint8_t *blob;
    size_t len;
    zip_entry *entries;
    int nentries;
} jar;

/* Parse the central directory of a jar image in memory. */
int jar_open(jar *j, const void *blob, size_t len);
void jar_close(jar *j);

/* Find an entry by exact name. Returns index or FX_ERR_NOTFOUND. */
int jar_find(const jar *j, const char *name);

/* Extract an entry into dst. Returns FX_OK and sets *outlen. */
int jar_extract(const jar *j, int idx, void *dst, size_t cap, size_t *outlen);

#endif /* FXJ2ME_JAR_H */
