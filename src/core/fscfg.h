/* fxj2me: shell configuration — persisted user settings.
 *
 * Stored as a small text file on the calculator's storage. Parsing and
 * formatting are pure functions (host-testable); load/save go through vfs.h.
 */
#ifndef FXJ2ME_FSCFG_H
#define FXJ2ME_FSCFG_H

#include <stddef.h>

#define CFG_MAX_PATH 64
#define CFG_PATH     "/FXJ2ME/CONFIG.TXT"

typedef struct fxj2me_cfg {
    char suites_dir[CFG_MAX_PATH]; /* AMS suite namespace (suite IDs) */
    char scan_dir[CFG_MAX_PATH];   /* where jars/JADs are searched    */
} fxj2me_cfg;

void cfg_defaults(fxj2me_cfg *c);

/* Parse config text; unknown/invalid lines are ignored. `c` is first reset to
 * defaults. Returns 0. */
int cfg_parse(fxj2me_cfg *c, const char *text, long len);

/* Format the config as text; returns bytes written (excluding NUL), or -1 if
 * the buffer is too small. */
long cfg_format(const fxj2me_cfg *c, char *buf, long cap);

/* Load from CFG_PATH (via vfs): 0 = loaded, 1 = defaults used (no file),
 * -1 = I/O error. Save: 0 = ok, -1 = error. */
int cfg_load(fxj2me_cfg *c);
int cfg_save(const fxj2me_cfg *c);

#endif /* FXJ2ME_FSCFG_H */
