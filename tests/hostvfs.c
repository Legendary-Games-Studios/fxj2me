/* fxj2me tests: host VFS backend over stdio/dirent.
 *
 * Maps "/..." paths onto a configured root directory on the host. */
#include "vfs.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char g_root[256];

/* Short-transfer simulation: the device filesystem returns partial reads and
 * writes, so tests can clamp every transfer to N bytes with HOSTVFS_CHUNK. */
static long chunk_limit(long len)
{
    static long lim = -2;

    if (lim == -2) {
        const char *s = getenv("HOSTVFS_CHUNK");
        lim = s ? atol(s) : 0;
        if (lim <= 0)
            lim = 0;
    }
    if (lim > 0 && len > lim)
        return lim;
    return len;
}

void hostvfs_set_root(const char *root)
{
    snprintf(g_root, sizeof g_root, "%s", root);
}

static void map_path(char *out, size_t cap, const char *path)
{
    snprintf(out, cap, "%s%s", g_root, path);
}

struct vfs_file {
    FILE *f;
};

struct vfs_dir {
    DIR *d;
    char dir[512];              /* mapped path, for per-entry stat */
};

int vfs_stat(const char *path, int *is_dir, uint32_t *size)
{
    char m[512];
    struct stat st;

    map_path(m, sizeof m, path);
    if (stat(m, &st) != 0)
        return -1;
    if (is_dir)
        *is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
    if (size)
        *size = (uint32_t)st.st_size;
    return 0;
}

int vfs_mkdir(const char *path)
{
    char m[512];

    map_path(m, sizeof m, path);
    if (mkdir(m, 0777) == 0 || errno == EEXIST)
        return 0;
    return -1;
}

int vfs_remove(const char *path)
{
    char m[512];

    map_path(m, sizeof m, path);
    return remove(m) == 0 ? 0 : -1;
}

vfs_dir *vfs_opendir(const char *path)
{
    vfs_dir *vd;
    char m[512];

    map_path(m, sizeof m, path);
    vd = malloc(sizeof *vd);
    if (!vd)
        return NULL;
    vd->d = opendir(m);
    if (!vd->d) {
        free(vd);
        return NULL;
    }
    snprintf(vd->dir, sizeof vd->dir, "%s", m);
    return vd;
}

int vfs_readdir(vfs_dir *d, vfs_dirent *out)
{
    struct dirent *e;
    struct stat st;
    char full[640];

    if (!d || !d->d)
        return -1;
    e = readdir(d->d);
    if (!e)
        return 0;

    {
        /* bounded copy: d_name has no fixed size */
        size_t k = strlen(e->d_name);
        if (k >= sizeof out->name)
            k = sizeof out->name - 1;
        memcpy(out->name, e->d_name, k);
        out->name[k] = 0;
    }
    snprintf(full, sizeof full, "%s/%s", d->dir, out->name);
    if (stat(full, &st) == 0) {
        out->is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
        out->size = (uint32_t)st.st_size;
    } else {
        out->is_dir = (e->d_type == DT_DIR);
        out->size = 0;
    }
    return 1;
}

void vfs_closedir(vfs_dir *d)
{
    if (!d)
        return;
    if (d->d)
        closedir(d->d);
    free(d);
}

vfs_file *vfs_open(const char *path, int write)
{
    vfs_file *vf;
    char m[512];

    map_path(m, sizeof m, path);
    vf = malloc(sizeof *vf);
    if (!vf)
        return NULL;
    vf->f = fopen(m, write ? "wb" : "rb");
    if (!vf->f) {
        free(vf);
        return NULL;
    }
    return vf;
}

long vfs_read(vfs_file *f, void *buf, long len)
{
    size_t n;
    if (!f || !f->f)
        return -1;
    n = fread(buf, 1, (size_t)chunk_limit(len), f->f);
    return (long)n;
}

long vfs_write(vfs_file *f, const void *buf, long len)
{
    size_t n;
    if (!f || !f->f)
        return -1;
    n = fwrite(buf, 1, (size_t)chunk_limit(len), f->f);
    return (long)n;
}

long vfs_seek(vfs_file *f, long off)
{
    if (!f || !f->f)
        return -1;
    if (fseek(f->f, off, SEEK_SET) != 0)
        return -1;
    return ftell(f->f);
}

void vfs_close(vfs_file *f)
{
    if (!f)
        return;
    if (f->f)
        fclose(f->f);
    free(f);
}
