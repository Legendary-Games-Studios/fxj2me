/* fxj2me: installed-suite package — the jar unpacked into storage.
 *
 * A phone's AMS installs a suite and keeps its contents available for
 * execution; this is how fxj2me does it. At install every jar entry is
 * written out as its own file under the suite's package directory, and the
 * runtime loads a class by opening just that file. So:
 *
 *   - no jar image is held in RAM at launch (only the class being parsed),
 *   - no seek/index arithmetic is needed: lookup is a path, loading is
 *     open + read (the shell already relied on both, unlike lseek on the
 *     calculator's filesystem),
 *   - a class that fails to load can be named exactly ("missing" vs
 *     "unreadable" file).
 *
 * Entry names are flattened into file names: jar paths use '/', which is not
 * legal inside a name, so it becomes '%'. "META-INF/MANIFEST.MF" is stored as
 * "META-INF%MANIFEST.MF".
 */
#ifndef FXJ2ME_PKG_H
#define FXJ2ME_PKG_H

#include "jar.h"

#define PKG_DIR_NAME   "CLS"
#define PKG_NAME_MAX   96

/* Encode a jar entry name ("a/b/C.class") into its file name ("a%b%C.class").
 * Returns 0, or -1 if the encoded name does not fit in `cap`. */
int pkg_encode(char *dst, size_t cap, const char *name);

/* Unpack every file entry of `j` into directory `dir` (created by the caller).
 * Entries are CRC-verified while being written. Returns 0 or -1. */
int pkg_build(const jar *j, const char *dir);

/* Size of an unpacked entry, or -1 if it is not there. */
long pkg_size(const char *dir, const char *name);

/* Read an entry into `dst` (must fit `cap`). Returns the byte count, -1 if
 * absent, -2 on a short/failed read. */
long pkg_read(const char *dir, const char *name, void *dst, size_t cap);

/* Allocate and read an entry (caller frees). FX_OK / FX_ERR_NOTFOUND /
 * FX_ERR_NOSPACE / FX_ERR_FORMAT. */
int pkg_extract(const char *dir, const char *name, uint8_t **buf, size_t *len);

#endif /* FXJ2ME_PKG_H */
