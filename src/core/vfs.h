/* fxj2me: virtual filesystem interface for the shell (scan/install/config).
 *
 * Paths use '/' separators and are rooted at the calculator's storage root:
 * "/FXJ2ME/APPS/GAME.JAR". Two backends implement this interface:
 *   - tests/hostvfs.c      (host stdio/dirent, for ctest)
 *   - fx9860/src/vfs_posix.c (gint's POSIX layer on the Fugue fs)
 * The interface is deliberately tiny; heavier logic (copying, scanning,
 * config persistence) lives in portable core code on top of it.
 */
#ifndef FXJ2ME_VFS_H
#define FXJ2ME_VFS_H

#include <stdint.h>

typedef struct vfs_file vfs_file;
typedef struct vfs_dir vfs_dir;

typedef struct vfs_dirent {
    char name[64];
    int is_dir;
    uint32_t size;
} vfs_dirent;

/* stat(): 0 on success, -1 if the path does not exist / error.
 * Either out pointer may be NULL. */
int vfs_stat(const char *path, int *is_dir, uint32_t *size);

/* mkdir(): 0 on success, -1 on error (already existing counts as success). */
int vfs_mkdir(const char *path);

/* Remove a file (or empty directory). 0 on success. */
int vfs_remove(const char *path);

/* Directory iteration. */
vfs_dir *vfs_opendir(const char *path);
int vfs_readdir(vfs_dir *d, vfs_dirent *out);   /* 1 = entry, 0 = end, -1 = error */
void vfs_closedir(vfs_dir *d);

/* File I/O. write=0 opens for reading (NULL if missing); write=1 creates or
 * truncates. vfs_read/vfs_write return the byte count or -1. vfs_seek moves
 * the absolute offset and returns it. */
vfs_file *vfs_open(const char *path, int write);
long vfs_read(vfs_file *f, void *buf, long len);
long vfs_write(vfs_file *f, const void *buf, long len);
long vfs_seek(vfs_file *f, long off);
void vfs_close(vfs_file *f);

#endif /* FXJ2ME_VFS_H */
