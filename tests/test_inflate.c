/* fxj2me tests: DEFLATE decompressor against zlib-generated vectors. */
#include "inflate.h"
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
    static const char *modes[] = { "fast", "best", "stored" };
    int i, m;

    for (i = 0; i < 10; i++) {
        for (m = 0; m < 3; m++) {
            char dp[512], cp[512];
            unsigned char *data, *comp, *out;
            size_t dlen = 0, clen = 0;
            inf_ctx c;
            int r;

            snprintf(dp, sizeof dp, "%s/data_%d_%s.bin", dir, i, modes[m]);
            snprintf(cp, sizeof cp, "%s/comp_%d_%s.bin", dir, i, modes[m]);
            data = read_file(dp, &dlen);
            comp = read_file(cp, &clen);
            if (!data || !comp) {
                printf("FAIL: missing fixtures %s / %s\n", dp, cp);
                failures++;
                free(data);
                free(comp);
                continue;
            }
            out = malloc(dlen + 16);
            inf_init(&c, comp, clen, out, dlen + 16);
            r = inf_run(&c);
            check(r == FX_OK, dp);
            if (r == FX_OK) {
                check(c.dstpos == dlen, "output length");
                check(memcmp(out, data, dlen) == 0, "output bytes");
            }
            free(data);
            free(comp);
            free(out);
        }
    }

    /* error cases: truncated and corrupted streams must not succeed */
    {
        unsigned char junk[64];
        unsigned char out[64];
        inf_ctx c;
        memset(junk, 0xFF, sizeof junk);
        inf_init(&c, junk, sizeof junk, out, sizeof out);
        check(inf_run(&c) != FX_OK, "corrupt stream rejected");
    }

    if (failures) {
        printf("test_inflate: %d failures\n", failures);
        return 1;
    }
    printf("test_inflate: OK\n");
    return 0;
}
