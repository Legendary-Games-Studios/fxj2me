/* fxj2me tests: interpreter smoke test against javac-generated classes. */
#include "vm.h"
#include <math.h>
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

static vm V;
static jthread T;
static char errbuf[256];

static int call(const char *m, const char *desc, const jslot *args, jslot *ret)
{
    int r = vm_call_static(&V, &T, "Fib", m, desc, args, ret);
    if (r != FX_OK) {
        snprintf(errbuf, sizeof errbuf, "call %s%s -> %d (%s)", m, desc, r,
                 T.error ? T.error : "?");
        check(0, errbuf);
    }
    return r;
}

static int32_t call_i(const char *m, const char *desc, const jslot *args)
{
    jslot ret[2];
    ret[0].i = 0xDEAD;
    if (call(m, desc, args, ret) != FX_OK)
        return 0;
    return ret[0].i;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "fixtures";
    char path[512];
    unsigned char *blob;
    size_t len = 0;
    jar j;
    static unsigned char heap[512 * 1024];
    jslot args[8], ret[2];

    snprintf(path, sizeof path, "%s/test.jar", dir);
    blob = read_file(path, &len);
    if (!blob) {
        printf("FAIL: cannot read %s\n", path);
        return 1;
    }
    check(jar_open(&j, blob, len) == FX_OK, "jar_open");
    {
        /* Optional tighter budget (FXJ2ME_TEST_HEAP=<bytes>) to simulate the
         * calculator's malloc heap on the host. */
        size_t hsz = sizeof heap;
        const char *env = getenv("FXJ2ME_TEST_HEAP");
        if (env && *env)
            hsz = (size_t)strtoul(env, NULL, 0);
        if (hsz < 1024 || hsz > sizeof heap)
            hsz = sizeof heap;
        /* no boot class library here: core-level interpreter test */
        check(vm_init(&V, heap, hsz, NULL, &j) == FX_OK, "vm_init");
    }
    memset(&T, 0, sizeof T);
    T.vm = &V;
    V.thread = &T;

    /* integer recursion + branches */
    args[0].i = 15;
    check(call_i("fib", "(I)I", args) == 610, "fib(15)");
    args[0].i = 1;
    check(call_i("fib", "(I)I", args) == 1, "fib(1)");

    /* loops + iinc */
    args[0].i = 100;
    check(call_i("sumLoop", "(I)I", args) == 5050, "sumLoop(100)");

    /* long arithmetic */
    args[0].i = 12;
    check(call("factLong", "(I)J", args, ret) == FX_OK &&
          jslot_get_long(ret) == 479001600LL, "factLong(12)");
    args[0].i = 20;
    check(call("factLong", "(I)J", args, ret) == FX_OK &&
          jslot_get_long(ret) == 2432902008176640000LL, "factLong(20)");

    /* arrays */
    args[0].i = 10;
    check(call_i("arraySum", "(I)I", args) == 285, "arraySum(10)");
    args[0].i = 4;
    check(call_i("matrix", "(I)I", args) == 36, "matrix(4)");

    /* byte array passed from C */
    {
        jref ba = heap_alloc(&V.heap, 5, NULL, JBLK_ARRAY, 5);
        int8_t *p;
        check(ba != JREF_NULL, "byte array alloc");
        p = (int8_t *)heap_ptr(&V.heap, ba);
        p[0] = 1; p[1] = 2; p[2] = 3; p[3] = 4; p[4] = -5;
        args[0].r = ba;
        check(call_i("bytes", "([B)I", args) == 5, "bytes()");
    }

    /* switches */
    args[0].i = 0;
    check(call_i("switchy", "(I)I", args) == 100, "switchy(0)");
    args[0].i = 5;
    check(call_i("switchy", "(I)I", args) == 500, "switchy(5)");
    args[0].i = 3;
    check(call_i("switchy", "(I)I", args) == -1, "switchy(3) default");
    args[0].i = -7;
    check(call_i("lookupSwitchy", "(I)I", args) == 7, "lookupSwitchy(-7)");
    args[0].i = 1000;
    check(call_i("lookupSwitchy", "(I)I", args) == 1000, "lookupSwitchy(1000)");
    args[0].i = 1;
    check(call_i("lookupSwitchy", "(I)I", args) == 0, "lookupSwitchy default");

    /* bit/arith mix */
    args[0].i = 7;
    args[1].i = 3;
    check(call_i("arithmetic", "(II)I", args) == 56, "arithmetic(7,3)");

    /* floats */
    args[0].f = 2.0f;
    check(call("floats", "(F)F", args, ret) == FX_OK &&
          fabsf(ret[0].f - 6.0f) < 1e-6f, "floats(2.0)");

    /* doubles */
    args[0].f = 0;   /* placeholder, real value below */
    jslot_set_double(args, 1.0);
    args[2].i = 4;
    check(call("doubles", "(DI)D", args, ret) == FX_OK &&
          fabs(jslot_get_double(ret) - 2.0833333333333335) < 1e-9,
          "doubles(1.0, 4)");

    /* statics */
    args[0].i = 5;
    check(call_i("statics", "(I)I", args) == 5, "statics(5)");
    args[0].i = 7;
    check(call_i("statics", "(I)I", args) == 12, "statics(7) accumulates");

    /* objects: new + <init> + invokevirtual + fields */
    args[0].i = 4;
    check(call_i("objects", "(I)I", args) == 10, "objects(4)");
    args[0].i = 500;
    check(call_i("objects", "(I)I", args) == 125250, "objects(500)");

    /* wide args */
    jslot_set_long(args, 100);
    args[2].i = 3;
    check(call_i("wideArgs", "(JI)I", args) == 381, "wideArgs(100, 3)");

    /* mixed wide locals */
    jslot_set_long(args, 10);
    jslot_set_double(&args[2], 2.0);
    args[4].i = 3;
    check(call_i("mixes", "(JDI)I", args) == 18, "mixes(10, 2.0, 3)");

    /* runtime faults propagate as errors */
    args[0].i = -1;
    check(vm_call_static(&V, &T, "Fib", "arraySum", "(I)I", args, ret) ==
              FX_ERR_JAVA, "negative array size raises Java error");

    /* load failures name the offending class (surfaced on the Run screen) */
    check(vm_load_class(&V, "com/ea/nowhere/Gone") == NULL,
          "missing class load fails");
    check(strstr(V.errbuf, "com/ea/nowhere/Gone") != NULL,
          "missing class name in error");
    check(vm_load_class(&V, "HasMissingParent") == NULL,
          "missing superclass load fails");
    check(strstr(V.errbuf, "Stub") != NULL,
          "missing superclass name in error");

    /* A class that is listed but cannot be read must not claim to be missing
     * (that exact lie sent a real MIDlet debug down the wrong path). */
    {
        static vm V2;
        static jthread T2;
        static unsigned char heap2[64 * 1024];
        unsigned char *bad = malloc(len);
        jar j2;
        size_t eocd = 0, pos;
        uint32_t cd_off;
        int nentries, e, patched = 0;

        check(bad != NULL, "corrupt-jar copy");
        if (bad && len > 22) {
            memcpy(bad, blob, len);
            /* locate the end-of-central-directory record */
            for (eocd = len - 22;; eocd--) {
                if (bad[eocd] == 0x50 && bad[eocd + 1] == 0x4b &&
                    bad[eocd + 2] == 0x05 && bad[eocd + 3] == 0x06)
                    break;
                if (eocd == 0)
                    break;
            }
            cd_off = (uint32_t)bad[eocd + 16] |
                     ((uint32_t)bad[eocd + 17] << 8) |
                     ((uint32_t)bad[eocd + 18] << 16) |
                     ((uint32_t)bad[eocd + 19] << 24);
            nentries = bad[eocd + 10] | (bad[eocd + 11] << 8);

            /* break Fib.class's local-header offset: the entry stays listed
             * in the directory but extraction now fails */
            pos = cd_off;
            for (e = 0; e < nentries; e++) {
                int nlen = bad[pos + 28] | (bad[pos + 29] << 8);
                if (memcmp(bad + pos + 46, "Fib.class", 9) == 0) {
                    uint32_t broken = (uint32_t)len - 30;
                    bad[pos + 42] = (unsigned char)broken;
                    bad[pos + 43] = (unsigned char)(broken >> 8);
                    bad[pos + 44] = (unsigned char)(broken >> 16);
                    bad[pos + 45] = (unsigned char)(broken >> 24);
                    patched = 1;
                    break;
                }
                pos += 46 + (size_t)nlen;
            }
            check(patched, "corrupt-jar target entry found");

            check(jar_open(&j2, bad, len) == FX_OK, "corrupt-jar jar_open");
            check(vm_init(&V2, heap2, sizeof heap2, NULL, &j2) == FX_OK,
                  "corrupt-jar vm_init");
            memset(&T2, 0, sizeof T2);
            T2.vm = &V2;
            V2.thread = &T2;

            check(vm_load_class(&V2, "Fib") == NULL, "unreadable entry fails");
            check(strstr(V2.errbuf, "unreadable") != NULL,
                  "unreadable entry says unreadable");
            check(strstr(V2.errbuf, "Fib") != NULL,
                  "unreadable entry names the class");
            check(strstr(V2.errbuf, "missing") == NULL,
                  "unreadable entry does not say missing");
            check(vm_load_class(&V2, "Nope") == NULL,
                  "missing in corrupt jar fails");
            check(strstr(V2.errbuf, "missing class") != NULL,
                  "genuinely missing class still says missing");

            vm_free(&V2);
            jar_close(&j2);
        }
        free(bad);
    }

    /* Class bytes that extract cleanly but are not a class file say "bad
     * class file" (never "out of memory"); a wrong CRC is caught in the jar
     * reader and reported as an unreadable entry. Minimal stored-entry zip:
     * one "Bad.class" entry holding 8 garbage bytes (crc32 0x4df50f33). */
    {
        static const unsigned char tinyzip[] = {
            /* local file header (30 bytes) */
            0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x33, 0x0f, 0xf5, 0x4d,                 /* crc32        */
            0x08, 0x00, 0x00, 0x00,                 /* compressed   */
            0x08, 0x00, 0x00, 0x00,                 /* uncompressed */
            0x09, 0x00, 0x00, 0x00,                 /* name len,ext */
            'B', 'a', 'd', '.', 'c', 'l', 'a', 's', 's',
            0xde, 0xad, 0xbe, 0xef, 0x01, 0x02, 0x03, 0x04,
            /* central directory (46 + 9 bytes) */
            0x50, 0x4b, 0x01, 0x02, 0x14, 0x00, 0x14, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x33, 0x0f, 0xf5, 0x4d,                 /* crc32        */
            0x08, 0x00, 0x00, 0x00,                 /* compressed   */
            0x08, 0x00, 0x00, 0x00,                 /* uncompressed */
            0x09, 0x00, 0x00, 0x00, 0x00, 0x00,     /* nlen,ext,cmt */
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,                 /* local offset */
            'B', 'a', 'd', '.', 'c', 'l', 'a', 's', 's',
            /* end of central directory */
            0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x01, 0x00, 0x37, 0x00, 0x00, 0x00,
            0x2f, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        static vm V3;
        static jthread T3;
        static unsigned char heap3[64 * 1024];
        unsigned char copy[sizeof tinyzip];
        jar j3;

        memcpy(copy, tinyzip, sizeof tinyzip);
        check(jar_open(&j3, copy, sizeof copy) == FX_OK, "tiny-zip jar_open");
        check(vm_init(&V3, heap3, sizeof heap3, NULL, &j3) == FX_OK,
              "tiny-zip vm_init");
        memset(&T3, 0, sizeof T3);
        T3.vm = &V3;
        V3.thread = &T3;

        check(vm_load_class(&V3, "Bad") == NULL, "garbage class bytes fail");
        check(strstr(V3.errbuf, "bad class file") != NULL,
              "garbage class bytes say bad class file");
        check(strstr(V3.errbuf, "out of memory") == NULL,
              "garbage class bytes do not claim OOM");
        vm_free(&V3);
        jar_close(&j3);

        copy[63] ^= 0x01;          /* flip one bit of the entry's CRC */
        check(jar_open(&j3, copy, sizeof copy) == FX_OK, "badcrc jar_open");
        check(vm_init(&V3, heap3, sizeof heap3, NULL, &j3) == FX_OK,
              "badcrc vm_init");
        check(vm_load_class(&V3, "Bad") == NULL, "bad crc load fails");
        check(strstr(V3.errbuf, "unreadable") != NULL,
              "bad crc says unreadable entry");
        check(strstr(V3.errbuf, "bad class file") == NULL,
              "crc damage caught before parse");
        vm_free(&V3);
        jar_close(&j3);
    }

    if (failures) {
        printf("test_vm: %d failures\n", failures);
        return 1;
    }
    printf("test_vm: OK\n");
    return 0;
}
