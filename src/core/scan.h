/* fxj2me: .jar discovery for the installer (portable, vfs-based). */
#ifndef FXJ2ME_SCAN_H
#define FXJ2ME_SCAN_H

#include <stdint.h>

#define SCAN_MAX_DEPTH 10

typedef struct jar_entry {
    char path[96];      /* full path to the jar file  */
    char name[32];      /* display name (file name minus ".jar") */
    uint32_t size;
} jar_entry;

typedef struct jar_list {
    jar_entry *items;   /* caller-provided storage */
    int count;
    int cap;
    int truncated;      /* more jars existed than fit in the list */
} jar_list;

typedef int (*scan_progress_fn)(const char *path, int found, void *ud);
/* Called for every visited entry; return nonzero to cancel the scan. */

void jar_list_init(jar_list *l, jar_entry *items, int cap);

/*--- path helpers ------------------------------------------------------*/

/* Join dir + "/" + name (no slash after root "/"). Returns 0, or -1 if the
 * result would not fit in `cap`. */
int path_join(char *dst, long cap, const char *dir, const char *name);

/* Pointer to the final component of a path. */
const char *path_base(const char *path);

/* Case-insensitive ".jar" extension test. */
int name_is_jar(const char *name);

/* Display name: file name with any ".jar" extension removed. */
void name_display(char *dst, long cap, const char *file_name);

/*--- discovery ---------------------------------------------------------*/

/* Recursively scan `root` for .jar files (directories up to SCAN_MAX_DEPTH
 * deep). Results are sorted by display name. Returns 0 = complete,
 * 1 = cancelled via progress, -1 = bad arguments. */
int scan_jars(const char *root, jar_list *out, scan_progress_fn progress,
              void *ud);

#endif /* FXJ2ME_SCAN_H */
