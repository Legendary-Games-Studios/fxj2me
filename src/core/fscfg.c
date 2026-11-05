/* fxj2me: shell configuration. */
#include "fscfg.h"
#include "vfs.h"

#include <string.h>

void cfg_defaults(fxj2me_cfg *c)
{
    strcpy(c->suites_dir, "/FXJ2ME/SUITES");
    strcpy(c->scan_dir, "/");
}

/* A config path must be absolute ('/'-rooted), fit in CFG_MAX_PATH, and stay
 * inside the storage root. */
static int valid_path(const char *s, long n)
{
    long i;
    if (n < 1 || n >= CFG_MAX_PATH || s[0] != '/')
        return 0;
    if (n >= 2 && s[n - 1] == '/' && n != 1)
        return 0;                       /* no trailing slash except root */
    for (i = 0; i + 1 < n; i++) {
        if (s[i] == '.' && s[i + 1] == '.')
            return 0;                   /* no ".." components */
    }
    return 1;
}

static long trim(const char *s, long n, long *start)
{
    long b = 0, e = n;
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r'))
        b++;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r'))
        e--;
    *start = b;
    return e - b;
}

int cfg_parse(fxj2me_cfg *c, const char *text, long len)
{
    long pos = 0;

    cfg_defaults(c);

    while (pos < len) {
        long eol = pos, eq = -1, i;

        while (eol < len && text[eol] != '\n')
            eol++;
        for (i = pos; i < eol; i++) {
            if (text[i] == '=') {
                eq = i;
                break;
            }
        }

        if (eq >= 0) {
            /* key lives in text[pos..eq), value in text[eq+1..eol) */
            long ks, kl, vs, vl;
            kl = trim(text + pos, eq - pos, &ks);
            vl = trim(text + eq + 1, eol - eq - 1, &vs);

            if (kl == 10 && memcmp(text + pos + ks, "suites_dir", 10) == 0 &&
                valid_path(text + eq + 1 + vs, vl)) {
                memcpy(c->suites_dir, text + eq + 1 + vs, (size_t)vl);
                c->suites_dir[vl] = 0;
            } else if (kl == 8 &&
                       memcmp(text + pos + ks, "scan_dir", 8) == 0 &&
                       valid_path(text + eq + 1 + vs, vl)) {
                memcpy(c->scan_dir, text + eq + 1 + vs, (size_t)vl);
                c->scan_dir[vl] = 0;
            }
            /* unknown keys and invalid values are ignored */
        }
        /* lines without '=' (comments, blanks) are ignored */

        pos = eol + 1;
    }
    return 0;
}

long cfg_format(const fxj2me_cfg *c, char *buf, long cap)
{
    long n = 0;
    long al = (long)strlen(c->suites_dir);
    long sl = (long)strlen(c->scan_dir);
    long need = 23 + 11 + al + 1 + 9 + sl + 1;  /* header + key=value lines */

    if (need > cap)
        return -1;

#define APPEND(str, slen)                                                   \
    do {                                                                    \
        memcpy(buf + n, (str), (size_t)(slen));                             \
        n += (slen);                                                        \
    } while (0)

    APPEND("# fxj2me configuration\n", 23);
    APPEND("suites_dir=", 11);
    APPEND(c->suites_dir, al);
    APPEND("\n", 1);
    APPEND("scan_dir=", 9);
    APPEND(c->scan_dir, sl);
    APPEND("\n", 1);
#undef APPEND

    buf[n] = 0;
    return n;
}

int cfg_load(fxj2me_cfg *c)
{
    vfs_file *f;
    char buf[512];
    long n;

    cfg_defaults(c);

    if (vfs_stat(CFG_PATH, NULL, NULL) != 0)
        return 1;

    f = vfs_open(CFG_PATH, 0);
    if (!f)
        return -1;
    n = vfs_read(f, buf, (long)sizeof buf - 1);
    vfs_close(f);
    if (n < 0)
        return -1;

    cfg_parse(c, buf, n);
    return 0;
}

int cfg_save(const fxj2me_cfg *c)
{
    vfs_file *f;
    char buf[512];
    long n;

    n = cfg_format(c, buf, (long)sizeof buf);
    if (n < 0)
        return -1;

    /* ensure the config directory exists */
    vfs_mkdir("/FXJ2ME");

    f = vfs_open(CFG_PATH, 1);
    if (!f)
        return -1;
    if (vfs_write(f, buf, n) != n) {
        vfs_close(f);
        return -1;
    }
    vfs_close(f);
    return 0;
}
