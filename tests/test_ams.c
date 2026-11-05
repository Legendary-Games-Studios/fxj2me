/* fxj2me tests: application management (install/upgrade/list/uninstall).
 *
 * Exercises the phone-style flow over the host VFS: install from a jar using
 * the manifest, install/upgrade through a JAD descriptor (size validation,
 * attribute precedence), reject bad descriptors, reject suites whose MIDlet
 * classes are not in the jar, and uninstall the whole namespace.
 *
 * usage: test_ams <fixtures-dir>       (needs hello.jar)
 */
#include "ams.h"
#include "jar.h"
#include "manifest.h"
#include "midp.h"
#include "vfs.h"
#include "vm.h"

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

/*--- helpers ------------------------------------------------------------*/

static void wf(const char *path, const void *data, long len)
{
    vfs_file *f = vfs_open(path, 1);
    long done = 0;

    if (!f) {
        check(0, path);
        return;
    }
    while (done < len) {                /* short writes happen on the device */
        long w = vfs_write(f, (const char *)data + done, len - done);
        if (w <= 0) {
            check(0, path);
            break;
        }
        done += w;
    }
    vfs_close(f);
}

static int exists(const char *path)
{
    return vfs_stat(path, NULL, NULL) == 0;
}

static long fsize(const char *path)
{
    uint32_t n = 0;
    if (vfs_stat(path, NULL, &n) != 0)
        return -1;
    return (long)n;
}

/* copy a host file into the VFS tree */
static int seed(const char *host_path, const char *vfs_path)
{
    FILE *f = fopen(host_path, "rb");
    static char buf[65536];
    size_t n;

    if (!f)
        return -1;
    n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (n == 0 || n == sizeof buf)
        return -1;
    wf(vfs_path, buf, (long)n);
    return (int)n;
}

static uint32_t crc32_tiny(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    size_t i;
    int k;

    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static void be16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Build a one-entry stored zip holding `name` = `data`. Returns its length. */
static size_t make_zip(uint8_t *out, const char *name, const char *data)
{
    size_t nlen = strlen(name), dlen = strlen(data);
    uint32_t crc = crc32_tiny((const uint8_t *)data, dlen);
    size_t p = 0, cdir;
    uint8_t *h;

    /* local file header */
    h = out + p;
    be32(h, 0x04034b50); be16(h + 4, 20); be16(h + 6, 0); be16(h + 8, 0);
    be16(h + 10, 0); be16(h + 12, 0); be32(h + 14, crc);
    be32(h + 18, (uint32_t)dlen); be32(h + 22, (uint32_t)dlen);
    be16(h + 26, (unsigned)nlen); be16(h + 28, 0);
    p += 30;
    memcpy(out + p, name, nlen); p += nlen;
    memcpy(out + p, data, dlen); p += dlen;
    cdir = p;

    /* central directory */
    h = out + p;
    be32(h, 0x02014b50); be16(h + 4, 20); be16(h + 6, 20); be16(h + 8, 0);
    be16(h + 10, 0); be16(h + 12, 0); be16(h + 14, 0); be32(h + 16, crc);
    be32(h + 20, (uint32_t)dlen); be32(h + 24, (uint32_t)dlen);
    be16(h + 28, (unsigned)nlen); be16(h + 30, 0); be16(h + 32, 0);
    be16(h + 34, 0); be16(h + 36, 0); be32(h + 38, 0); be32(h + 42, 0);
    p += 46;
    memcpy(out + p, name, nlen); p += nlen;

    /* end of central directory */
    h = out + p;
    be32(h, 0x06054b50); be16(h + 4, 0); be16(h + 6, 0);
    be16(h + 8, 1); be16(h + 10, 1); be32(h + 12, (uint32_t)(p + 22 - cdir));
    be32(h + 16, (uint32_t)cdir); be16(h + 20, 0);
    p += 22;
    return p;
}

/* Install a synthetic jar made of `manifest` only. */
static int install_synthetic(const char *vfs_jar, const char *manifest,
                             const char *root, char *err)
{
    static uint8_t zip[4096];
    size_t n = make_zip(zip, "META-INF/MANIFEST.MF", manifest);
    wf(vfs_jar, zip, (long)n);
    err[0] = 0;
    return ams_install(vfs_jar, NULL, root, NULL, err, 64);
}

/*--- tests --------------------------------------------------------------*/

static void test_install_from_jar(const char *fix)
{
    char err[64], p[96];
    ams_suite s;

    check(seed(fix, "/IN/hello.jar") > 0, "seed hello.jar");
    err[0] = 0;
    check(ams_install("/IN/hello.jar", NULL, "/S", &s, err, sizeof err) == FX_OK,
          "install from manifest");
    if (err[0])
        printf("  (install error: %s)\n", err);

    check(s.id == 1, "first suite gets ID 1");
    check(strcmp(s.name, "Hello") == 0, "suite name from MIDlet-Name");
    check(strcmp(s.version, "1.0.0") == 0, "suite version");
    check(strcmp(s.vendor, "Acme") == 0, "suite vendor");
    check(s.nmidlets == 1, "one MIDlet");

    check(ams_jar_path(p, sizeof p, "/S", 1) == 0, "jar path");
    check(fsize(p) == fsize("/IN/hello.jar"), "jar copied");
    check(exists("/S/0001/SUITE.MF"), "suite record written");
    check(exists("/S/0001/CLS/HelloMidlet.class"), "class unpacked to storage");
    check(exists("/S/0001/DATA"), "suite data namespace created");

    /* the record round-trips into a launchable MIDlet */
    {
        midlet_info info;
        check(ams_load_midlet("/S", 1, 0, &info) == FX_OK, "load MIDlet 0");
        check(strcmp(info.cls, "HelloMidlet") == 0, "launch class");
        check(strcmp(info.midlet_name, "Hello") == 0, "record name");
        check(strcmp(info.vendor, "Acme") == 0, "record vendor");
        check(ams_load_midlet("/S", 1, 1, &info) != FX_OK,
              "MIDlet index 1 rejected");
    }
}

static void test_list(void)
{
    static ams_suite items[4];
    ams_list l;

    ams_list_init(&l, items, 4);
    check(ams_list_suites("/S", &l) == 0, "list suites");
    check(l.count == 1, "one installed suite");
    if (l.count == 1) {
        check(strcmp(l.items[0].name, "Hello") == 0, "listed name");
        check(strcmp(l.items[0].version, "1.0.0") == 0, "listed version");
        check(l.items[0].nmidlets == 1, "listed MIDlet count");
    }
}

static void test_jad(void)
{
    char err[64];
    ams_suite s;
    long jsize = fsize("/IN/hello.jar");
    char jad[256];

    /* descriptor attributes win, and the size must match the jar */
    snprintf(jad, sizeof jad,
             "MIDlet-Name: Hello\r\n"
             "MIDlet-Version: 2.0.0\r\n"
             "MIDlet-Vendor: Acme\r\n"
             "MIDlet-1: Hello2, , HelloMidlet\r\n"
             "MIDlet-Jar-URL: hello.jar\r\n"
             "MIDlet-Jar-Size: %ld\r\n"
             "MicroEdition-Configuration: CLDC-1.1\r\n"
             "MicroEdition-Profile: MIDP-2.0\r\n"
             "\r\n", jsize);
    wf("/IN/hello.jad", jad, (long)strlen(jad));
    err[0] = 0;
    check(ams_install("/IN/hello.jar", "/IN/hello.jad", "/S", &s, err,
                      sizeof err) == FX_OK, "install with JAD");
    check(s.id == 1, "upgrade keeps the suite ID");
    check(strcmp(s.version, "2.0.0") == 0, "JAD version overrides manifest");
    check(exists("/S/0001/APP.JAD"), "descriptor stored");

    {
        static ams_suite items[4];
        ams_list l;
        ams_list_init(&l, items, 4);
        ams_list_suites("/S", &l);
        check(l.count == 1, "upgrade replaces, does not duplicate");
    }
    {
        midlet_info info;
        check(ams_load_midlet("/S", 1, 0, &info) == FX_OK, "load upgraded");
        check(strcmp(info.name, "Hello2") == 0, "JAD MIDlet-1 overrides");
        check(strcmp(info.version, "2.0.0") == 0, "record version updated");
    }

    /* size mismatch: phones refuse the install */
    snprintf(jad, sizeof jad,
             "MIDlet-Name: Hello\r\n"
             "MIDlet-Jar-Size: %ld\r\n\r\n", jsize + 1);
    wf("/IN/bad.jad", jad, (long)strlen(jad));
    err[0] = 0;
    check(ams_install("/IN/hello.jar", "/IN/bad.jad", "/S2", NULL, err,
                      sizeof err) != FX_OK, "size mismatch refused");
    check(strcmp(err, "JAD/JAR size mismatch") == 0, "size mismatch named");
    check(!exists("/S2/0001/APP.JAR"), "nothing written on refusal");

    /* a descriptor without MIDlet-Jar-Size is not installable */
    wf("/IN/nosize.jad", "MIDlet-Name: Hello\r\n\r\n", 28);
    err[0] = 0;
    check(ams_install("/IN/hello.jar", "/IN/nosize.jad", "/S2", NULL, err,
                      sizeof err) != FX_OK, "missing size refused");
    check(strcmp(err, "MIDlet-Jar-Size missing") == 0, "missing size named");

    /* unsupported profile (must list a profile the device implements) */
    snprintf(jad, sizeof jad,
             "MIDlet-Jar-Size: %ld\r\n"
             "MicroEdition-Profile: MIDP-3.0\r\n\r\n", jsize);
    wf("/IN/prof.jad", jad, (long)strlen(jad));
    err[0] = 0;
    check(ams_install("/IN/hello.jar", "/IN/prof.jad", "/S2", NULL, err,
                      sizeof err) != FX_OK, "unsupported profile refused");
    check(strcmp(err, "unsupported profile") == 0, "profile error named");
}

static void test_rejections(void)
{
    char err[64];

    /* MIDlet class missing from the jar */
    err[0] = 0;
    check(install_synthetic("/IN/noclass.jar",
                            "Manifest-Version: 1.0\r\n"
                            "MIDlet-Name: Nope\r\n"
                            "MIDlet-Version: 1.0\r\n"
                            "MIDlet-Vendor: Acme\r\n"
                            "MIDlet-1: Nope, , NoSuchClass\r\n"
                            "\r\n", "/S3", err) != FX_OK,
          "missing class refused");
    check(strncmp(err, "missing class: NoSuchClass", 25) == 0,
          "missing class named");

    /* required suite attributes */
    err[0] = 0;
    check(install_synthetic("/IN/noname.jar",
                            "Manifest-Version: 1.0\r\n"
                            "MIDlet-Version: 1.0\r\n"
                            "MIDlet-Vendor: Acme\r\n"
                            "MIDlet-1: X, , HelloMidlet\r\n"
                            "\r\n", "/S3", err) != FX_OK,
          "MIDlet-Name required");
    check(strcmp(err, "MIDlet-Name missing") == 0, "name error named");

    err[0] = 0;
    check(install_synthetic("/IN/nover.jar",
                            "Manifest-Version: 1.0\r\n"
                            "MIDlet-Name: X\r\n"
                            "MIDlet-Vendor: Acme\r\n"
                            "MIDlet-1: X, , HelloMidlet\r\n"
                            "\r\n", "/S3", err) != FX_OK,
          "MIDlet-Version required");
    check(strcmp(err, "MIDlet-Version missing") == 0, "version error named");

    err[0] = 0;
    check(install_synthetic("/IN/novendor.jar",
                            "Manifest-Version: 1.0\r\n"
                            "MIDlet-Name: X\r\n"
                            "MIDlet-Version: 1.0\r\n"
                            "MIDlet-1: X, , HelloMidlet\r\n"
                            "\r\n", "/S3", err) != FX_OK,
          "MIDlet-Vendor required");
    check(strcmp(err, "MIDlet-Vendor missing") == 0, "vendor error named");

    err[0] = 0;
    check(install_synthetic("/IN/nomidlet.jar",
                            "Manifest-Version: 1.0\r\n"
                            "MIDlet-Name: X\r\n"
                            "MIDlet-Version: 1.0\r\n"
                            "MIDlet-Vendor: Acme\r\n"
                            "\r\n", "/S3", err) != FX_OK,
          "MIDlet-1 required");
    check(strcmp(err, "no MIDlet-1") == 0, "MIDlet-1 error named");

    /* not a jar at all */
    wf("/IN/nope.txt", "hello", 5);
    err[0] = 0;
    check(ams_install("/IN/nope.txt", NULL, "/S3", NULL, err, sizeof err) != FX_OK,
          "non-jar refused");
}

static void test_multi_midlet(void)
{
    char err[64], jad[256];
    long n;

    /* Two MIDlets in one suite, declared by the descriptor: a suite may carry
     * as many MIDlet-<n> entries as it likes, and the jar only needs the
     * classes they name. */
    n = fsize("/IN/hello.jar");
    snprintf(jad, sizeof jad,
             "MIDlet-Name: Pair\r\n"
             "MIDlet-Version: 3.1\r\n"
             "MIDlet-Vendor: Acme\r\n"
             "MIDlet-1: One, , HelloMidlet\r\n"
             "MIDlet-2: Two, /icon.png, HelloMidlet\r\n"
             "MIDlet-Jar-Size: %ld\r\n\r\n", n);
    wf("/IN/pair.jad", jad, (long)strlen(jad));
    err[0] = 0;
    check(ams_install("/IN/hello.jar", "/IN/pair.jad", "/S4", NULL, err,
                      sizeof err) == FX_OK, "install two-MIDlet suite");
    if (err[0])
        printf("  (multi error: %s)\n", err);
    {
        midlet_info info;
        check(ams_load_midlet("/S4", 1, 1, &info) == FX_OK, "load MIDlet 2");
        check(strcmp(info.name, "Two") == 0, "MIDlet-2 name");
        check(strcmp(info.icon, "/icon.png") == 0, "MIDlet-2 icon");
        check(strcmp(info.cls, "HelloMidlet") == 0, "MIDlet-2 class");
    }
}

static void test_uninstall(void)
{
    static ams_suite items[4];
    ams_list l;

    check(ams_uninstall("/S", 1) == 0, "uninstall");
    check(!exists("/S/0001"), "suite namespace removed");
    ams_list_init(&l, items, 4);
    ams_list_suites("/S", &l);
    check(l.count == 0, "list empty after uninstall");

    /* the ID is reusable once the namespace is gone */
    {
        char err[64];
        ams_suite s;
        err[0] = 0;
        check(ams_install("/IN/hello.jar", NULL, "/S", &s, err,
                          sizeof err) == FX_OK, "reinstall after uninstall");
        check(s.id == 1, "ID reused");
    }
}

/* End to end: a suite installed by the AMS is launchable exactly like a
 * phone would launch it — the record supplies attributes and the MIDlet
 * class, and the VM boots with the classes unpacked in storage. */
static void test_launch(const char *boot_path)
{
    static unsigned char heap[1024 * 1024];
    static vm V;
    static jthread T;
    unsigned char *bootb = NULL;
    size_t blen = 0;
    jar boot;
    midlet_info info;
    FILE *f;
    int i;
    char err[64];

    check(ams_install("/IN/hello.jar", NULL, "/S5", NULL, err,
                      sizeof err) == FX_OK, "install for launch");

    f = fopen(boot_path, "rb");
    if (!f) {
        printf("SKIP: no boot.jar at %s\n", boot_path);
        return;
    }
    fseek(f, 0, SEEK_END);
    blen = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    bootb = blen ? malloc(blen) : NULL;
    check(bootb != NULL, "boot buffer");
    if (bootb)
        check(fread(bootb, 1, blen, f) == blen, "boot.jar read");
    fclose(f);
    if (!bootb)
        return;

    check(jar_open(&boot, bootb, blen) == FX_OK, "boot jar opens");

    /* The runtime form: classes are unpacked in flash storage, so no jar is
     * resident in RAM. */
    check(pkg_size("/S5/0001/CLS", "HelloMidlet.class") > 0,
          "class is unpacked in storage");
    check(ams_load_midlet("/S5", 1, 0, &info) == FX_OK, "record loads MIDlet");
    check(strcmp(info.cls, "HelloMidlet") == 0, "record class");

    check(vm_init(&V, heap, sizeof heap, &boot, NULL) == FX_OK, "vm_init");
    snprintf(V.app_dir, sizeof V.app_dir, "%s", "/S5/0001/CLS");
    memset(&T, 0, sizeof T);
    T.vm = &V;
    V.thread = &T;
    if (midp_start(&V, &T, &info) == FX_OK) {
        int ink = 0;

        for (i = 0; i < 3; i++)
            check(midp_poll() == FX_OK, "pump step");
        {
            const uint8_t *fr = midp_frame();
            for (i = 0; i < MIDP_FRAME_SIZE; i++)
                if (fr[i])
                    ink = 1;
            check(ink, "installed MIDlet painted");
        }
        midp_stop();
    } else {
        check(0, "MIDlet starts from record");
        printf("  (start error: %s)\n", midp_error());
    }
    vm_free(&V);
    jar_close(&boot);
    free(bootb);
}

int main(int argc, char **argv)
{
    char fixture[512];
    char host_root[64], vfs_root[64];

    snprintf(vfs_root, sizeof vfs_root, "/amstmp-%d", (int)getpid());
    snprintf(host_root, sizeof host_root, "amstmp-%d", (int)getpid());

    hostvfs_set_root(".");
    check(vfs_mkdir(vfs_root) == 0, "create test root");
    hostvfs_set_root(host_root);
    vfs_mkdir("/IN");
    vfs_mkdir("/S");
    vfs_mkdir("/S2");
    vfs_mkdir("/S3");
    vfs_mkdir("/S4");
    vfs_mkdir("/S5");

    snprintf(fixture, sizeof fixture, "%s/hello.jar",
             argc > 1 ? argv[1] : "fixtures");

    test_install_from_jar(fixture);
    test_list();
    test_jad();
    test_rejections();
    test_multi_midlet();
    test_launch(argc > 2 ? argv[2] : "boot.jar");
    test_uninstall();

    if (failures) {
        printf("test_ams: %d failures\n", failures);
        return 1;
    }
    printf("test_ams: OK\n");
    return 0;
}
