/* fxj2me tests: ZIP/jar reader against a generated test.jar. */
#include "jar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static unsigned char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long sz;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(sz ? (size_t)sz : 1);
    if (buf && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf)
        *len = (size_t)sz;
    return buf;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "fixtures";
    char path[512];
    unsigned char *blob;
    size_t len = 0;
    jar j;
    int idx;
    unsigned char out[65536];
    size_t outlen = 0;

    snprintf(path, sizeof path, "%s/test.jar", dir);
    blob = read_file(path, &len);
    if (!blob) {
        printf("FAIL: cannot read %s\n", path);
        return 1;
    }

    check(jar_open(&j, blob, len) == FX_OK, "jar_open");
    check(j.nentries >= 3, "entry count");

    idx = jar_find(&j, "hello.txt");
    check(idx >= 0, "find hello.txt");
    if (idx >= 0) {
        check(jar_extract(&j, idx, out, sizeof out, &outlen) == FX_OK,
              "extract hello.txt");
        check(outlen > 0 && out[0] == 'h', "hello.txt contents");
    }

    idx = jar_find(&j, "plain.txt");
    check(idx >= 0, "find plain.txt (stored)");
    if (idx >= 0) {
        check(jar_extract(&j, idx, out, sizeof out, &outlen) == FX_OK,
              "extract plain.txt");
        check(outlen == 13 && memcmp(out, "stored entry\n", 13) == 0,
              "plain.txt contents");
    }

    idx = jar_find(&j, "Fib.class");
    check(idx >= 0, "find Fib.class (deflated class)");
    if (idx >= 0) {
        check(jar_extract(&j, idx, out, sizeof out, &outlen) == FX_OK,
              "extract Fib.class");
        check(outlen > 4 && out[0] == 0xCA && out[1] == 0xFE &&
              out[2] == 0xBA && out[3] == 0xBE, "class magic");
    }

    check(jar_find(&j, "nope.txt") == FX_ERR_NOTFOUND, "missing entry");

    jar_close(&j);

    /* truncated blob must fail gracefully */
    check(jar_open(&j, blob, 40) != FX_OK, "truncated jar rejected");

    free(blob);
    if (failures) {
        printf("test_jar: %d failures\n", failures);
        return 1;
    }
    printf("test_jar: OK\n");
    return 0;
}
