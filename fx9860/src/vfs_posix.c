/* fxj2me device: VFS backend over gint's POSIX layer (Fugue fs, fx G-III).
 *
 * The OS filesystem cannot be touched from gint context, so every operation
 * runs inside gint_world_switch(). Stack-allocated argument blocks are safe
 * because the switch is synchronous. Handles are malloc'd outside the
 * switches. */
#include <gint/gint.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vfs.h"

struct vfs_file {
    int fd;
};

struct vfs_dir {
    DIR *dp;
    char path[128];
};

/*--- thunks (executed in OS mode) --------------------------------------*/

typedef struct {
    const char *path;
    int *is_dir;
    uint32_t *size;
    int ret;
} st_args;

static void t_stat(void *vp)
{
    st_args *a = vp;
    struct stat st;

    if (stat(a->path, &st) != 0) {
        a->ret = -1;
        return;
    }
    if (a->is_dir)
        *a->is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
    if (a->size)
        *a->size = (uint32_t)st.st_size;
    a->ret = 0;
}

typedef struct {
    const char *path;
    int ret;
} path_args;

static void t_mkdir(void *vp)
{
    path_args *a = vp;
    a->ret = (mkdir(a->path, 0777) == 0 || errno == EEXIST) ? 0 : -1;
}

static void t_remove(void *vp)
{
    path_args *a = vp;
    a->ret = remove(a->path) == 0 ? 0 : -1;
}

typedef struct {
    const char *path;
    DIR *dp;
} dir_args;

static void t_opendir(void *vp)
{
    dir_args *a = vp;
    a->dp = opendir(a->path);
}

typedef struct {
    DIR *dp;
    const char *dir;
    vfs_dirent *out;
    int ret;
} rd_args;

static void t_readdir(void *vp)
{
    rd_args *a = vp;
    struct dirent *e = readdir(a->dp);
    struct stat st;
    char full[192];
    size_t k;

    if (!e) {
        a->ret = 0;
        return;
    }

    k = strlen(e->d_name);
    if (k >= sizeof a->out->name)
        k = sizeof a->out->name - 1;
    memcpy(a->out->name, e->d_name, k);
    a->out->name[k] = 0;

    snprintf(full, sizeof full, "%s/%s", a->dir, a->out->name);
    if (stat(full, &st) == 0) {
        a->out->is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
        a->out->size = (uint32_t)st.st_size;
    } else {
        a->out->is_dir = (e->d_type == DT_DIR);
        a->out->size = 0;
    }
    a->ret = 1;
}

static void t_closedir(void *vp)
{
    DIR **d = vp;
    closedir(*d);
}

typedef struct {
    const char *path;
    int flags;
    int fd;
} open_args;

static void t_open(void *vp)
{
    open_args *a = vp;
    a->fd = open(a->path, a->flags, 0666);
}

typedef struct {
    int fd;
    void *buf;
    long len;
    long ret;
} rw_args;

static void t_read(void *vp)
{
    rw_args *a = vp;
    a->ret = (long)read(a->fd, a->buf, (size_t)a->len);
}

static void t_write(void *vp)
{
    rw_args *a = vp;
    a->ret = (long)write(a->fd, a->buf, (size_t)a->len);
}

typedef struct {
    int fd;
    long off;
    long ret;
} seek_args;

static void t_seek(void *vp)
{
    seek_args *a = vp;
    a->ret = (long)lseek(a->fd, (off_t)a->off, SEEK_SET);
}

static void t_closefd(void *vp)
{
    int *fd = vp;
    close(*fd);
}

/*--- interface ---------------------------------------------------------*/

int vfs_stat(const char *path, int *is_dir, uint32_t *size)
{
    st_args a = { path, is_dir, size, -1 };
    gint_world_switch(GINT_CALL(t_stat, (void *)&a));
    return a.ret;
}

int vfs_mkdir(const char *path)
{
    path_args a = { path, -1 };
    gint_world_switch(GINT_CALL(t_mkdir, (void *)&a));
    return a.ret;
}

int vfs_remove(const char *path)
{
    path_args a = { path, -1 };
    gint_world_switch(GINT_CALL(t_remove, (void *)&a));
    return a.ret;
}

vfs_dir *vfs_opendir(const char *path)
{
    dir_args a = { path, NULL };
    vfs_dir *d;

    gint_world_switch(GINT_CALL(t_opendir, (void *)&a));
    if (!a.dp)
        return NULL;

    d = malloc(sizeof *d);
    if (!d) {
        gint_world_switch(GINT_CALL(t_closedir, (void *)&a.dp));
        return NULL;
    }
    d->dp = a.dp;
    snprintf(d->path, sizeof d->path, "%s", path);
    return d;
}

int vfs_readdir(vfs_dir *d, vfs_dirent *out)
{
    rd_args a;
    if (!d)
        return -1;
    a.dp = d->dp;
    a.dir = d->path;
    a.out = out;
    a.ret = -1;
    gint_world_switch(GINT_CALL(t_readdir, (void *)&a));
    return a.ret;
}

void vfs_closedir(vfs_dir *d)
{
    if (!d)
        return;
    if (d->dp)
        gint_world_switch(GINT_CALL(t_closedir, (void *)&d->dp));
    free(d);
}

vfs_file *vfs_open(const char *path, int write)
{
    open_args a;
    vfs_file *f;

    a.path = path;
    a.flags = write ? (O_WRONLY | O_CREAT | O_TRUNC) : O_RDONLY;
    a.fd = -1;
    gint_world_switch(GINT_CALL(t_open, (void *)&a));
    if (a.fd < 0)
        return NULL;

    f = malloc(sizeof *f);
    if (!f) {
        gint_world_switch(GINT_CALL(t_closefd, (void *)&a.fd));
        return NULL;
    }
    f->fd = a.fd;
    return f;
}

long vfs_read(vfs_file *f, void *buf, long len)
{
    rw_args a;
    if (!f)
        return -1;
    a.fd = f->fd;
    a.buf = buf;
    a.len = len;
    a.ret = -1;
    gint_world_switch(GINT_CALL(t_read, (void *)&a));
    return a.ret;
}

long vfs_write(vfs_file *f, const void *buf, long len)
{
    rw_args a;
    if (!f)
        return -1;
    a.fd = f->fd;
    a.buf = (void *)buf;
    a.len = len;
    a.ret = -1;
    gint_world_switch(GINT_CALL(t_write, (void *)&a));
    return a.ret;
}

long vfs_seek(vfs_file *f, long off)
{
    seek_args a;
    if (!f)
        return -1;
    a.fd = f->fd;
    a.off = off;
    a.ret = -1;
    gint_world_switch(GINT_CALL(t_seek, (void *)&a));
    return a.ret;
}

void vfs_close(vfs_file *f)
{
    if (!f)
        return;
    gint_world_switch(GINT_CALL(t_closefd, (void *)&f->fd));
    free(f);
}
