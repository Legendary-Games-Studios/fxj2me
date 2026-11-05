/* fxj2me tests: the installed-suite package (jar unpacked to files).
 *
 * Builds a package from a real jar and checks that every entry is present as
 * its own file, reads back byte-identically, that name encoding round-trips,
 * and that missing entries / undersized buffers are reported properly.
 *
 * usage: test_pkg <fixtures-dir>        (needs hello.jar)
 */
#include "jar.h"
#include "pkg.h"
#include "vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void hostvfs_set_root(const char *root);

static int failures = 0;

static void check(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static unsigned char *read_host(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long n;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

int main(int argc, char **argv)
{
    char fixture[512], host_root[64], vfs_root[64];
    unsigned char *blob;
    size_t blen = 0;
    jar j;
    int i, entries = 0;

    snprintf(vfs_root, sizeof vfs_root, "/pkgtmp-%d", (int)getpid());
    snprintf(host_root, sizeof host_root, "pkgtmp-%d", (int)getpid());
    hostvfs_set_root(".");
    check(vfs_mkdir(vfs_root) == 0, "create test root");
    hostvfs_set_root(host_root);
    check(vfs_mkdir("/PKG") == 0, "create package dir");

    /* name encoding: jar paths cannot be file names */
    {
        char name[64];
        check(pkg_encode(name, sizeof name, "META-INF/MANIFEST.MF") == 0 &&
                  strcmp(name, "META-INF%MANIFEST.MF") == 0,
              "encode manifest path");
        check(pkg_encode(name, sizeof name, "HelloMidlet.class") == 0 &&
                  strcmp(name, "HelloMidlet.class") == 0,
              "encode flat name");
        check(pkg_encode(name, 8, "HelloMidlet.class") == -1,
              "encode overflow rejected");
    }

    snprintf(fixture, sizeof fixture, "%s/hello.jar",
             argc > 1 ? argv[1] : "fixtures");
    blob = read_host(fixture, &blen);
    if (!blob) {
        printf("SKIP: no %s\n", fixture);
        return 77;
    }
    check(jar_open(&j, blob, blen) == FX_OK, "jar opens");
    check(pkg_build(&j, "/PKG") == 0, "package builds");

    /* every entry is its own file and round-trips byte for byte */
    for (i = 0; i < j.nentries; i++) {
        const zip_entry *e = &j.entries[i];
        unsigned char *want = NULL, *got = NULL;
        size_t want_len = 0;
        long n;

        if (e->name[0] == 0 || e->name[strlen(e->name) - 1] == '/' ||
            e->usize == 0)
            continue;
        entries++;
        want = malloc(e->usize);
        check(want && jar_extract(&j, i, want, e->usize, &want_len) == FX_OK,
              "source entry extracts");
        n = pkg_size("/PKG", e->name);
        check(n == (long)e->usize, "package size matches");
        got = malloc(e->usize);
        n = got ? pkg_read("/PKG", e->name, got, e->usize) : -1;
        check(n == (long)e->usize, "package entry reads");
        if (want && got && n == (long)e->usize)
            check(memcmp(want, got, (size_t)n) == 0, "package entry content");
        free(want);
        free(got);
    }
    check(entries > 0, "package stored classes");
    check(pkg_size("/PKG", "HelloMidlet.class") > 0, "class file unpacked");

    /* undersized destination is refused, not truncated */
    check(pkg_read("/PKG", "HelloMidlet.class", (char[8]){0}, 8) == -2,
          "undersized read refused");

    /* missing entries */
    check(pkg_size("/PKG", "NoSuch.class") == -1, "missing entry size");
    check(pkg_read("/PKG", "NoSuch.class", (char[4]){0}, 4) == -1,
          "missing entry read");
    {
        uint8_t *buf = NULL;
        size_t len = 0;
        check(pkg_extract("/PKG", "NoSuch.class", &buf, &len) == FX_ERR_NOTFOUND,
              "extract reports not found");
        check(buf == NULL, "nothing allocated for a miss");
        check(pkg_extract("/PKG", "HelloMidlet.class", &buf, &len) == FX_OK &&
                  len > 4,
              "extract class");
        check(buf != NULL && buf[0] == 0xca && buf[1] == 0xfe &&
                  buf[2] == 0xba && buf[3] == 0xbe,
              "extracted class has class-file magic");
        free(buf);
    }

    /* an unpacked name with no file behind it (deleted entry) */
    check(vfs_remove("/PKG/HelloMidlet.class") == 0, "remove unpacked entry");
    check(pkg_extract("/PKG", "HelloMidlet.class", &(uint8_t *){0},
                      &(size_t){0}) != FX_OK,
          "removed entry is not found");

    free(blob);
    jar_close(&j);
    if (failures) {
        printf("test_pkg: %d failures\n", failures);
        return 1;
    }
    printf("test_pkg: OK (%d entries)\n", entries);
    return 0;
}
