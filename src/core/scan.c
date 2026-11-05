/* fxj2me: .jar discovery, installation and app listing. */
#include "scan.h"
#include "vfs.h"

#include <stdlib.h>
#include <string.h>

void jar_list_init(jar_list *l, jar_entry *items, int cap)
{
    l->items = items;
    l->count = 0;
    l->cap = cap;
    l->truncated = 0;
}

int path_join(char *dst, long cap, const char *dir, const char *name)
{
    long dl = (long)strlen(dir);
    long nl = (long)strlen(name);
    int slash = !(dl == 1 && dir[0] == '/');
    long total = dl + (slash ? 1 : 0) + nl;

    if (total + 1 > cap)
        return -1;
    memcpy(dst, dir, (size_t)dl);
    if (slash)
        dst[dl] = '/';
    memcpy(dst + dl + (slash ? 1 : 0), name, (size_t)nl);
    dst[total] = 0;
    return 0;
}

const char *path_base(const char *path)
{
    const char *p = path, *b = path;
    for (; *p; p++) {
        if (*p == '/')
            b = p + 1;
    }
    return b;
}

int name_is_jar(const char *name)
{
    long n = (long)strlen(name);
    const char *e;
    if (n < 5)                          /* >= 1 char + ".jar" */
        return 0;
    e = name + n - 4;
    return e[0] == '.' &&
           (e[1] == 'j' || e[1] == 'J') &&
           (e[2] == 'a' || e[2] == 'A') &&
           (e[3] == 'r' || e[3] == 'R');
}

void name_display(char *dst, long cap, const char *file_name)
{
    long n = (long)strlen(file_name);
    if (name_is_jar(file_name))
        n -= 4;
    if (n > cap - 1)
        n = cap - 1;
    memcpy(dst, file_name, (size_t)n);
    dst[n] = 0;
}

static int ci_cmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'a' && ca <= 'z')
            ca -= 32;
        if (cb >= 'a' && cb <= 'z')
            cb -= 32;
        if (ca != cb)
            return ca < cb ? -1 : 1;
        a++;
        b++;
    }
    return *a ? 1 : (*b ? -1 : 0);
}

static int entry_cmp(const void *a, const void *b)
{
    return ci_cmp(((const jar_entry *)a)->name, ((const jar_entry *)b)->name);
}

typedef struct walk_state {
    jar_list *out;
    scan_progress_fn progress;
    void *ud;
    int recurse;
    int cancelled;
} walk_state;

static void walk(walk_state *w, const char *dir, int depth)
{
    vfs_dir *d;
    vfs_dirent e;
    char child[96];

    d = vfs_opendir(dir);
    if (!d)
        return;                         /* unreadable: skip quietly */

    while (!w->cancelled && vfs_readdir(d, &e) == 1) {
        if (e.name[0] == '.' &&
            (e.name[1] == 0 || (e.name[1] == '.' && e.name[2] == 0)))
            continue;

        if (path_join(child, (long)sizeof child, dir, e.name) != 0)
            continue;

        if (e.is_dir) {
            if (w->recurse && depth + 1 <= SCAN_MAX_DEPTH)
                walk(w, child, depth + 1);
        } else if (name_is_jar(e.name)) {
            if (w->out->count >= w->out->cap) {
                w->out->truncated = 1;
            } else {
                jar_entry *it = &w->out->items[w->out->count];
                memcpy(it->path, child, strlen(child) + 1);
                name_display(it->name, (long)sizeof it->name, e.name);
                it->size = e.size;
                w->out->count++;
            }
        }

        if (w->progress && w->progress(child, w->out->count, w->ud) != 0)
            w->cancelled = 1;
    }
    vfs_closedir(d);
}

int scan_jars(const char *root, jar_list *out, scan_progress_fn progress,
              void *ud)
{
    walk_state w;

    if (!root || !out)
        return -1;

    w.out = out;
    w.progress = progress;
    w.ud = ud;
    w.recurse = 1;
    w.cancelled = 0;

    walk(&w, root, 0);
    qsort(out->items, (size_t)out->count, sizeof(jar_entry), entry_cmp);
    return w.cancelled ? 1 : 0;
}


