/* fxj2me: installed-suite package (jar unpacked to files in storage). */
#include "pkg.h"
#include "scan.h"
#include "vfs.h"

#include <stdlib.h>
#include <string.h>

/* Storage transfers can be short (Fugue moves data in OS-sized chunks), so
 * every read/write is looped until it completes. */
static int read_full(vfs_file *f, void *dst, long n)
{
    uint8_t *p = dst;
    long got = 0;

    while (got < n) {
        long r = vfs_read(f, p + got, n - got);
        if (r <= 0)
            return -1;
        got += r;
    }
    return 0;
}

static int write_full(vfs_file *f, const void *src, long n)
{
    const uint8_t *p = src;
    long done = 0;

    while (done < n) {
        long w = vfs_write(f, p + done, n - done);
        if (w <= 0)
            return -1;
        done += w;
    }
    return 0;
}

int pkg_encode(char *dst, size_t cap, const char *name)
{
    size_t n = strlen(name), i;

    if (n + 1 > cap || n > PKG_NAME_MAX)
        return -1;
    for (i = 0; i < n; i++)
        dst[i] = name[i] == '/' ? '%' : name[i];
    dst[n] = 0;
    return 0;
}

static int entry_path(char *dst, size_t cap, const char *dir, const char *name)
{
    char file[PKG_NAME_MAX + 1];

    if (pkg_encode(file, sizeof file, name) != 0)
        return -1;
    return path_join(dst, (long)cap, dir, file);
}

int pkg_build(const jar *j, const char *dir)
{
    int i;

    if (!j || !dir)
        return -1;

    for (i = 0; i < j->nentries; i++) {
        const zip_entry *e = &j->entries[i];
        uint8_t *buf;
        vfs_file *f;
        char path[160];
        size_t got = 0;
        int ok = 0;

        if (e->name[0] == 0 || e->name[strlen(e->name) - 1] == '/')
            continue;                   /* directories have no content */
        if (e->usize == 0)
            continue;
        if (entry_path(path, sizeof path, dir, e->name) != 0)
            return -1;

        buf = malloc(e->usize);
        if (!buf)
            return -1;
        if (jar_extract(j, i, buf, e->usize, &got) != FX_OK ||
            got != e->usize) {
            free(buf);                  /* corrupt entry: refuse the package */
            return -1;
        }

        vfs_remove(path);
        f = vfs_open(path, 1);
        if (f) {
            ok = write_full(f, buf, (long)got) == 0;
            vfs_close(f);
        }
        free(buf);
        if (!ok) {
            vfs_remove(path);
            return -1;
        }
    }
    return 0;
}

long pkg_size(const char *dir, const char *name)
{
    char path[160];
    uint32_t size = 0;

    if (!dir || entry_path(path, sizeof path, dir, name) != 0)
        return -1;
    if (vfs_stat(path, NULL, &size) != 0)
        return -1;
    return (long)size;
}

long pkg_read(const char *dir, const char *name, void *dst, size_t cap)
{
    char path[160];
    vfs_file *f;
    uint32_t size = 0;
    long n;

    if (!dir || !dst || entry_path(path, sizeof path, dir, name) != 0)
        return -1;
    if (vfs_stat(path, NULL, &size) != 0 || size == 0)
        return -1;
    if (size > cap)
        return -2;
    f = vfs_open(path, 0);
    if (!f)
        return -2;
    n = read_full(f, dst, (long)size) == 0 ? (long)size : -2;
    vfs_close(f);
    return n;
}

int pkg_extract(const char *dir, const char *name, uint8_t **buf, size_t *len)
{
    char path[160];
    uint32_t size = 0;
    vfs_file *f;
    uint8_t *b;

    if (!buf || !len || !dir)
        return FX_ERR_FORMAT;
    if (entry_path(path, sizeof path, dir, name) != 0)
        return FX_ERR_NOTFOUND;
    if (vfs_stat(path, NULL, &size) != 0 || size == 0)
        return FX_ERR_NOTFOUND;
    b = malloc(size);
    if (!b)
        return FX_ERR_NOSPACE;
    f = vfs_open(path, 0);
    if (!f) {
        free(b);
        return FX_ERR_FORMAT;
    }
    if (read_full(f, b, (long)size) != 0) {
        vfs_close(f);
        free(b);
        return FX_ERR_FORMAT;
    }
    vfs_close(f);
    *buf = b;
    *len = size;
    return FX_OK;
}
