/* fxj2me: AMS — install/upgrade/list/uninstall MIDlet suites (portable, vfs). */
#include "ams.h"
#include "jar.h"
#include "pkg.h"
#include "scan.h"
#include "vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AMS_MAX_JAR 240 * 1024
#define AMS_JAD_MAX 4096
#define AMS_REC_MAX 1600

void ams_list_init(ams_list *l, ams_suite *items, int cap)
{
    l->items = items;
    l->count = 0;
    l->cap = cap;
    l->truncated = 0;
}

static void set_err(char *err, size_t cap, const char *msg)
{
    if (!err || cap == 0)
        return;
    snprintf(err, cap, "%s", msg);
}

static int fail(char *err, size_t cap, const char *msg)
{
    set_err(err, cap, msg);
    return -1;
}

/*--- paths --------------------------------------------------------------*/

static void num_dir(char *dst, size_t cap, int id)
{
    snprintf(dst, cap, "%04d", id);
}

int ams_suite_dir(char *dst, size_t cap, const char *root, int id)
{
    char name[16];
    num_dir(name, sizeof name, id);
    return path_join(dst, (long)cap, root, name);
}

int ams_jar_path(char *dst, size_t cap, const char *root, int id)
{
    char dir[112];
    if (ams_suite_dir(dir, sizeof dir, root, id) != 0)
        return -1;
    return path_join(dst, (long)cap, dir, AMS_JAR_FILE);
}

int ams_class_dir(char *dst, size_t cap, const char *root, int id)
{
    char dir[112];
    if (ams_suite_dir(dir, sizeof dir, root, id) != 0)
        return -1;
    return path_join(dst, (long)cap, dir, PKG_DIR_NAME);
}

/*--- small file helpers -------------------------------------------------*/

/* Storage transfers can be short (Fugue moves data in OS-sized chunks), so
 * every read/write here is looped. */
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

/* Read a small text file (record/descriptor) into `buf`, looping over short
 * reads until EOF or the buffer is full. */
static long read_small(const char *path, char *buf, long cap)
{
    vfs_file *f = vfs_open(path, 0);
    long got = 0;

    if (!f)
        return -1;
    while (got < cap - 1) {
        long r = vfs_read(f, buf + got, cap - 1 - got);
        if (r < 0) {
            vfs_close(f);
            return -1;
        }
        if (r == 0)
            break;
        got += r;
    }
    vfs_close(f);
    buf[got] = 0;
    return got;
}

static int copy_file(const char *src, const char *dst)
{
    vfs_file *in, *out;
    char buf[512];
    long n = 0;

    in = vfs_open(src, 0);
    if (!in)
        return -1;
    vfs_remove(dst);
    out = vfs_open(dst, 1);
    if (!out) {
        vfs_close(in);
        return -1;
    }
    for (;;) {
        n = vfs_read(in, buf, (long)sizeof buf);
        if (n < 0) {
            vfs_close(in);
            vfs_close(out);
            vfs_remove(dst);
            return -1;
        }
        if (n == 0)
            break;
        if (write_full(out, buf, n) != 0) {
            vfs_close(in);
            vfs_close(out);
            vfs_remove(dst);
            return -1;
        }
    }
    vfs_close(in);
    vfs_close(out);
    return 0;
}

static int ensure_dir(const char *path)
{
    int is_dir;
    if (vfs_stat(path, &is_dir, NULL) == 0)
        return is_dir ? 0 : -1;
    return vfs_mkdir(path);
}

/*--- descriptor validation ----------------------------------------------*/

/* Every token of a space/comma separated list must be a supported value
 * (MIDP: the device must implement all listed profiles/configuration). */
static int all_supported(const char *val, const char *const *ok, int n)
{
    while (val && *val) {
        const char *s;
        size_t len;
        int i, hit = 0;

        while (*val == ' ' || *val == '\t' || *val == ',')
            val++;
        if (!*val)
            break;
        s = val;
        while (*val && *val != ' ' && *val != '\t' && *val != ',')
            val++;
        len = (size_t)(val - s);
        for (i = 0; i < n; i++)
            if (strlen(ok[i]) == len && strncmp(s, ok[i], len) == 0)
                hit = 1;
        if (!hit)
            return 0;
    }
    return 1;
}

static int check_platform(const midlet_info *m, char *err, size_t cap)
{
    static const char *const profiles[] = { "MIDP-1.0", "MIDP-2.0" };
    static const char *const configs[] = { "CLDC-1.0", "CLDC-1.1" };
    const char *p = manifest_attr(m, "MicroEdition-Profile");
    const char *c = manifest_attr(m, "MicroEdition-Configuration");

    if (p && !all_supported(p, profiles, 2)) {
        set_err(err, cap, "unsupported profile");
        return -1;
    }
    if (c && !all_supported(c, configs, 2)) {
        set_err(err, cap, "unsupported configuration");
        return -1;
    }
    return 0;
}

/*--- install ------------------------------------------------------------*/

static int suite_id_ok(const char *name)
{
    int i, digits = 0;
    for (i = 0; name[i]; i++) {
        if (name[i] < '0' || name[i] > '9')
            return 0;
        digits++;
    }
    return digits > 0 && digits <= 4;
}

/* Pick the ID for this install: reuse the ID of a suite with the same
 * MIDlet-Name + MIDlet-Vendor (upgrade), else max+1. */
static int pick_id(const char *root, const char *name, const char *vendor,
                   int *upgrade)
{
    vfs_dir *d;
    vfs_dirent e;
    int max = 0, existing = 0;

    *upgrade = 0;
    d = vfs_opendir(root);
    if (!d)
        return 1;
    while (vfs_readdir(d, &e) == 1) {
        char rec[112];
        char txt[AMS_REC_MAX];
        int id;

        if (!e.is_dir || !suite_id_ok(e.name))
            continue;
        id = atoi(e.name);
        if (id > max)
            max = id;
        if (id <= 0)
            continue;
        if (path_join(rec, (long)sizeof rec, e.name, AMS_REC_FILE) != 0)
            continue;
        {
            char dir[112];
            if (ams_suite_dir(dir, sizeof dir, root, id) != 0)
                continue;
            if (path_join(rec, (long)sizeof rec, dir, AMS_REC_FILE) != 0)
                continue;
        }
        if (read_small(rec, txt, AMS_REC_MAX) <= 0)
            continue;
        {
            midlet_info *rec_info = calloc(1, sizeof *rec_info);
            if (!rec_info)
                continue;
            if (manifest_parse_text(txt, strlen(txt), rec_info) == FX_OK &&
                strcmp(rec_info->midlet_name, name) == 0 &&
                strcmp(rec_info->vendor, vendor) == 0) {
                existing = id;
                *upgrade = 1;
            }
            free(rec_info);
        }
        if (existing)
            break;
    }
    vfs_closedir(d);
    if (existing)
        return existing;
    if (max >= 9999)
        return -1;
    return max + 1;
}

/* Write the merged main-section attributes as "Key: Value" lines. */
static int write_record(const char *path, const midlet_info *m)
{
    vfs_file *f;
    size_t used = 0;
    int i;

    vfs_remove(path);
    f = vfs_open(path, 1);
    if (!f)
        return -1;
    for (i = 0; i < m->nattrs; i++) {
        const char *e = m->attrs + used;
        const char *eq = strchr(e, '=');
        used += strlen(e) + 1;
        if (!eq)
            continue;
        if (write_full(f, e, (long)(eq - e)) != 0 ||
            write_full(f, ": ", 2) != 0 ||
            write_full(f, eq + 1, (long)strlen(eq + 1)) != 0 ||
            write_full(f, "\r\n", 2) != 0) {
            vfs_close(f);
            vfs_remove(path);
            return -1;
        }
    }
    vfs_close(f);
    return 0;
}

int ams_install(const char *jar_path, const char *jad_path, const char *root,
                ams_suite *out, char *err, size_t errcap)
{
    midlet_info *mf = NULL, *jd = NULL, *mg = NULL;
    void *blob = NULL;
    jar j;
    uint32_t jsize = 0;
    int is_dir = 0, id, upgrade = 0, i;
    int r = -1;
    int jar_open_done = 0, have_jad = 0;
    char dir[112], dst[128];

    if (err && errcap)
        err[0] = 0;
    memset(&j, 0, sizeof j);

    if (!jar_path || !root) {
        fail(err, errcap, "no jar");
        goto cleanup;
    }
    if (vfs_stat(jar_path, &is_dir, &jsize) != 0 || is_dir || jsize == 0) {
        fail(err, errcap, "cannot read the jar");
        goto cleanup;
    }
    if (!name_is_jar(path_base(jar_path))) {
        fail(err, errcap, "not a jar");
        goto cleanup;
    }
    if (jsize > AMS_MAX_JAR) {
        fail(err, errcap, "jar too large");
        goto cleanup;
    }

    mf = malloc(sizeof *mf);
    mg = malloc(sizeof *mg);
    blob = malloc(jsize);
    if (!mf || !mg || !blob) {
        fail(err, errcap, "out of memory");
        goto cleanup;
    }
    {
        vfs_file *f = vfs_open(jar_path, 0);
        int r = f ? read_full(f, blob, (long)jsize) : -1;
        if (f)
            vfs_close(f);
        if (r != 0) {
            fail(err, errcap, "cannot read the jar");
            goto cleanup;
        }
    }
    if (jar_open(&j, blob, jsize) != FX_OK) {
        fail(err, errcap, "not a jar");
        goto cleanup;
    }
    jar_open_done = 1;

    if (manifest_parse(&j, mf) != FX_OK) {
        fail(err, errcap, "no manifest");
        goto cleanup;
    }

    if (jad_path && jad_path[0]) {
        char *txt = malloc(AMS_JAD_MAX);
        long n;

        if (!txt) {
            fail(err, errcap, "out of memory");
            goto cleanup;
        }
        n = read_small(jad_path, txt, AMS_JAD_MAX);
        if (n <= 0) {
            free(txt);
            fail(err, errcap, "cannot read the JAD");
            goto cleanup;
        }
        jd = malloc(sizeof *jd);
        if (!jd) {
            free(txt);
            fail(err, errcap, "out of memory");
            goto cleanup;
        }
        if (manifest_parse_text(txt, (size_t)n, jd) != FX_OK) {
            free(txt);
            fail(err, errcap, "bad JAD");
            goto cleanup;
        }
        free(txt);
        have_jad = 1;
        /* The descriptor MUST state the JAR size, and it must be the truth. */
        if (jd->jar_size == 0) {
            fail(err, errcap, "MIDlet-Jar-Size missing");
            goto cleanup;
        }
        if (jd->jar_size != jsize) {
            fail(err, errcap, "JAD/JAR size mismatch");
            goto cleanup;
        }
    }
    manifest_merge(mf, have_jad ? jd : NULL, mg);

    if (!mg->midlet_name[0]) {
        fail(err, errcap, "MIDlet-Name missing");
        goto cleanup;
    }
    if (!mg->version[0]) {
        fail(err, errcap, "MIDlet-Version missing");
        goto cleanup;
    }
    if (!mg->vendor[0]) {
        fail(err, errcap, "MIDlet-Vendor missing");
        goto cleanup;
    }
    if (mg->nmidlets == 0) {
        fail(err, errcap, "no MIDlet-1");
        goto cleanup;
    }
    if (check_platform(mg, err, errcap) != 0)
        goto cleanup;

    /* Every declared MIDlet class must actually be in the jar. */
    for (i = 0; i < mg->nmidlets; i++) {
        char ename[MF_CLASS_MAX + 8];
        snprintf(ename, sizeof ename, "%s.class", mg->midlets[i].cls);
        if (jar_find(&j, ename) < 0) {
            if (err && errcap)
                snprintf(err, errcap, "missing class: %.48s",
                         mg->midlets[i].cls);
            goto cleanup;
        }
    }

    if (ensure_dir(root) != 0) {
        fail(err, errcap, "cannot create storage");
        goto cleanup;
    }

    id = pick_id(root, mg->midlet_name, mg->vendor, &upgrade);
    if (id <= 0) {
        fail(err, errcap, "no free suite ID");
        goto cleanup;
    }
    if (ams_suite_dir(dir, sizeof dir, root, id) != 0) {
        fail(err, errcap, "path too long");
        goto cleanup;
    }
    if (ensure_dir(dir) != 0) {
        fail(err, errcap, "cannot create storage");
        goto cleanup;
    }

    if (path_join(dst, (long)sizeof dst, dir, AMS_JAR_FILE) != 0 ||
        copy_file(jar_path, dst) != 0) {
        fail(err, errcap, "out of space");
        goto cleanup;
    }

    /* Unpack the jar into the suite's class directory: execution then needs
     * no jar image in RAM, only the classes in use. */
    if (path_join(dst, (long)sizeof dst, dir, PKG_DIR_NAME) != 0) {
        fail(err, errcap, "path too long");
        goto cleanup;
    }
    if (ensure_dir(dst) != 0) {
        fail(err, errcap, "cannot create storage");
        goto cleanup;
    }
    if (pkg_build(&j, dst) != 0) {
        fail(err, errcap, "out of space");
        goto cleanup;
    }
    /* Read the unpacked class back before declaring success: a bad write on
     * storage must fail the install, not surface later as an unreadable
     * class on launch. */
    {
        char ename[MF_CLASS_MAX + 8];
        uint8_t *b = NULL;
        size_t n = 0;
        int ok = pkg_extract(dst, "META-INF/MANIFEST.MF", &b, &n) == FX_OK &&
                 n > 0;

        free(b);
        b = NULL;
        snprintf(ename, sizeof ename, "%s.class", mg->midlets[0].cls);
        if (ok)
            ok = pkg_extract(dst, ename, &b, &n) == FX_OK && n >= 4 &&
                 b[0] == 0xca && b[1] == 0xfe && b[2] == 0xba &&
                 b[3] == 0xbe;
        free(b);
        if (!ok) {
            fail(err, errcap, "package verify failed");
            goto cleanup;
        }
    }

    if (path_join(dst, (long)sizeof dst, dir, AMS_JAD_FILE) != 0) {
        fail(err, errcap, "path too long");
        goto cleanup;
    }
    if (have_jad) {
        if (copy_file(jad_path, dst) != 0) {
            fail(err, errcap, "out of space");
            goto cleanup;
        }
    } else {
        vfs_remove(dst);                    /* stale descriptor from upgrade */
    }

    if (path_join(dst, (long)sizeof dst, dir, AMS_REC_FILE) != 0 ||
        write_record(dst, mg) != 0) {
        fail(err, errcap, "out of space");
        goto cleanup;
    }

    if (path_join(dst, (long)sizeof dst, dir, AMS_DATA_DIR) != 0) {
        fail(err, errcap, "path too long");
        goto cleanup;
    }
    if (ensure_dir(dst) != 0) {             /* RMS namespace (unused for now) */
        fail(err, errcap, "cannot create storage");
        goto cleanup;
    }

    if (out) {
        out->id = id;
        memcpy(out->name, mg->midlet_name, sizeof out->name);
        memcpy(out->version, mg->version, sizeof out->version);
        memcpy(out->vendor, mg->vendor, sizeof out->vendor);
        out->nmidlets = mg->nmidlets;
        out->jar_size = jsize;
        out->data_size = mg->data_size;
    }
    r = FX_OK;
    (void)upgrade;

cleanup:
    if (jar_open_done)
        jar_close(&j);
    free(blob);
    free(mf);
    free(jd);
    free(mg);
    return r;
}

/*--- listing / loading --------------------------------------------------*/

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

static int suite_cmp(const void *a, const void *b)
{
    return ci_cmp(((const ams_suite *)a)->name, ((const ams_suite *)b)->name);
}

int ams_load_suite(const char *root, int id, midlet_info *out)
{
    char dir[112], rec[128], txt[AMS_REC_MAX];
    long n;

    if (!root || !out || id <= 0)
        return FX_ERR_NOTFOUND;
    if (ams_suite_dir(dir, sizeof dir, root, id) != 0)
        return FX_ERR_NOTFOUND;
    if (path_join(rec, (long)sizeof rec, dir, AMS_REC_FILE) != 0)
        return FX_ERR_NOTFOUND;
    n = read_small(rec, txt, AMS_REC_MAX);
    if (n <= 0)
        return FX_ERR_NOTFOUND;
    if (manifest_parse_text(txt, (size_t)n, out) != FX_OK)
        return FX_ERR_FORMAT;
    return out->nattrs > 0 ? FX_OK : FX_ERR_FORMAT;
}

int ams_load_midlet(const char *root, int id, int midx, midlet_info *out)
{
    int r = ams_load_suite(root, id, out);
    if (r != FX_OK)
        return r;
    if (midlet_select(out, midx) != 0)
        return FX_ERR_NOTFOUND;
    return FX_OK;
}

int ams_list_suites(const char *root, ams_list *out)
{
    vfs_dir *d;
    vfs_dirent e;

    if (!root || !out)
        return -1;
    if (out->items && out->cap > 0)
        memset(out->items, 0, (size_t)out->cap * sizeof *out->items);
    out->count = 0;
    out->truncated = 0;

    d = vfs_opendir(root);
    if (!d)
        return -1;
    while (vfs_readdir(d, &e) == 1) {
        char dir[112], rec[128], txt[AMS_REC_MAX];
        midlet_info *info;
        long n;
        int id;

        if (!e.is_dir || !suite_id_ok(e.name))
            continue;
        id = atoi(e.name);
        if (id <= 0)
            continue;
        if (ams_suite_dir(dir, sizeof dir, root, id) != 0)
            continue;
        if (path_join(rec, (long)sizeof rec, dir, AMS_REC_FILE) != 0)
            continue;
        n = read_small(rec, txt, AMS_REC_MAX);
        if (n <= 0)
            continue;
        info = calloc(1, sizeof *info);
        if (!info)
            continue;
        if (manifest_parse_text(txt, (size_t)n, info) == FX_OK) {
            if (out->count >= out->cap) {
                out->truncated = 1;
            } else {
                ams_suite *s = &out->items[out->count++];
                s->id = id;
                memcpy(s->name, info->midlet_name[0] ? info->midlet_name
                                                     : info->name,
                       sizeof s->name);
                memcpy(s->version, info->version, sizeof s->version);
                memcpy(s->vendor, info->vendor, sizeof s->vendor);
                s->nmidlets = info->nmidlets;
                s->jar_size = info->jar_size;
                s->data_size = info->data_size;
            }
        }
        free(info);
    }
    vfs_closedir(d);

    if (out->count > 1)
        qsort(out->items, (size_t)out->count, sizeof *out->items, suite_cmp);
    return 0;
}

/* Remove a flat directory's files, then the directory itself. */
static void remove_tree(const char *dir)
{
    vfs_dir *d = vfs_opendir(dir);

    if (d) {
        vfs_dirent e;
        while (vfs_readdir(d, &e) == 1) {
            char fp[192];
            if (e.is_dir)
                continue;               /* package entries are files */
            if (path_join(fp, (long)sizeof fp, dir, e.name) == 0)
                vfs_remove(fp);
        }
        vfs_closedir(d);
    }
    vfs_remove(dir);
}

int ams_uninstall(const char *root, int id)
{
    char dir[112], p[128];
    int is_dir;

    if (!root || id <= 0)
        return -1;
    if (ams_suite_dir(dir, sizeof dir, root, id) != 0)
        return -1;
    if (vfs_stat(dir, &is_dir, NULL) != 0)
        return -1;

    path_join(p, (long)sizeof p, dir, AMS_JAR_FILE);
    vfs_remove(p);
    path_join(p, (long)sizeof p, dir, AMS_JAD_FILE);
    vfs_remove(p);

    /* unpacked class/resources directory */
    path_join(p, (long)sizeof p, dir, PKG_DIR_NAME);
    remove_tree(p);
    path_join(p, (long)sizeof p, dir, AMS_REC_FILE);
    vfs_remove(p);

    /* suite data namespace (RMS) */
    path_join(p, (long)sizeof p, dir, AMS_DATA_DIR);
    remove_tree(p);
    return vfs_remove(dir) == 0 ? 0 : -1;
}
