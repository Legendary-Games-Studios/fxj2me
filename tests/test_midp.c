/* fxj2me tests: the MIDP runtime end to end. Launches the HelloMidlet
 * fixture from hello.jar (manifest-driven) against the boot library and
 * asserts rendered pixels, key delivery, callSerially and destruction.
 *
 * usage: test_midp <boot.jar> <hello.jar>
 */
#include "classfile.h"
#include "manifest.h"
#include "midp.h"
#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static vm V;
static jthread T;

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

static int px(const uint8_t *f, int x, int y)
{
    return (f[y * (MIDP_FRAME_W / 8) + (x >> 3)] >> (7 - (x & 7))) & 1;
}

static int region_has_black(const uint8_t *f, int x0, int y0, int x1, int y1)
{
    int x, y;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            if (px(f, x, y))
                return 1;
    return 0;
}

/* static int/boolean field of HelloMidlet */
static int32_t get_si(const char *field, const char *desc)
{
    jclass *c = vm_load_class(&V, "HelloMidlet");
    uint32_t slot;
    if (!c || class_find_field(c, field, desc, true, &slot, NULL) != FX_OK)
        return -1;
    return c->static_vals[slot].i;
}

/* static String field of HelloMidlet compared against ASCII text */
static int str_is(const char *field, const char *want)
{
    jclass *c = vm_load_class(&V, "HelloMidlet");
    uint32_t slot;
    bool is_ref;
    const uint16_t *chars;
    int len = 0, i, ok;

    if (!c ||
        class_find_field(c, field, "Ljava/lang/String;", true, &slot,
                         &is_ref) != FX_OK)
        return 0;
    if (vm_str_chars(&V, c->static_vals[slot].r, &chars, &len) != FX_OK)
        return 0;
    ok = (len == (int)strlen(want));
    for (i = 0; ok && i < len; i++)
        if (chars[i] != (uint16_t)(unsigned char)want[i])
            ok = 0;
    return ok;
}

int main(int argc, char **argv)
{
    const char *boot_path = argc > 1 ? argv[1] : "boot.jar";
    const char *app_path = argc > 2 ? argv[2] : "hello.jar";
    static unsigned char heap[2 * 1024 * 1024];
    unsigned char *boot_blob, *app_blob;
    size_t boot_len = 0, app_len = 0;
    jar boot, app;
    midlet_info info;
    const uint8_t *f;

    boot_blob = read_file(boot_path, &boot_len);
    app_blob = read_file(app_path, &app_len);
    if (!boot_blob || !app_blob) {
        printf("SKIP: missing %s / %s\n", boot_path, app_path);
        return 77;
    }
    check(jar_open(&boot, boot_blob, boot_len) == FX_OK, "jar_open boot");
    check(jar_open(&app, app_blob, app_len) == FX_OK, "jar_open app");

    /*--- manifest ---------------------------------------------------*/
    check(manifest_parse(&app, &info) == FX_OK, "manifest_parse");
    check(strcmp(info.cls, "HelloMidlet") == 0, "MIDlet-1 class");
    check(strcmp(info.name, "Hello") == 0, "MIDlet-1 name");
    check(strcmp(info.midlet_name, "Hello") == 0, "MIDlet-Name");
    check(strcmp(info.vendor, "Acme") == 0, "MIDlet-Vendor");
    check(manifest_attr(&info, "MicroEdition-Profile") != NULL,
          "attr lookup");
    {
        /* Real-world manifests wrap long values at 72 bytes across
         * continuation lines with CRLF endings. The literal must match
         * DESC in gen_fixtures.py. */
        static const char desc[] =
            "fxj2me fixture manifest, written the way the jar tool writes "
            "real ones: long values are wrapped across several continuation "
            "lines at seventy-two bytes";
        const char *d = manifest_attr(&info, "MIDlet-Description");
        check(d != NULL && strcmp(d, desc) == 0,
              "manifest continuations join");
    }

    /*--- launch -----------------------------------------------------*/
    check(vm_init(&V, heap, sizeof heap, &boot, &app) == FX_OK, "vm_init");
    memset(&T, 0, sizeof T);
    T.vm = &V;
    V.thread = &T;

    if (midp_start(&V, &T, &info) != FX_OK) {
        printf("FAIL: midp_start -> %s\n", midp_error());
        return 1;
    }
    check(str_is("vendor", "Acme"), "getAppProperty(MIDlet-Vendor)");
    check(str_is("midlet1", "Hello, , HelloMidlet"),
          "getAppProperty(MIDlet-1)");

    /*--- first pump: runnable + first paint --------------------------*/
    check(midp_poll() == FX_OK, "first poll");
    check(get_si("runnableRan", "Z") == 1, "callSerially runnable ran");
    check(get_si("paintCount", "I") == 1, "paint ran once");

    f = midp_frame();
    check(f != NULL, "frame available");
    if (f) {
        check(px(f, 0, 0) == 1, "drawRect border");
        check(region_has_black(f, 40, 10, 88, 18), "drawString text");
        check(px(f, 32, 42) == 0, "counter rect before key");
        check(px(f, 103, 50) == 1, "drawImage top row");
        check(px(f, 103, 52) == 0, "drawImage body white");
        check(px(f, 22, 57) == 1, "drawRegion ROT180 flipped row");
        check(px(f, 22, 52) == 0, "drawRegion body white");
        check(px(f, 64, 60) == 1, "drawLine");
        check(px(f, 66, 33) == 1, "fillTriangle");
        check(px(f, 95, 35) == 1, "fillArc");
        check(px(f, 10, 2) == 1, "drawRoundRect edge");
        check(px(f, 31, 31) == 1, "translate");
        check(px(f, 0, 50) == 1 && px(f, 1, 50) == 0, "drawRGB row 0");
        check(px(f, 0, 51) == 0 && px(f, 1, 51) == 1, "drawRGB row 1");
        check(px(f, 66, 57) == 1, "clip keeps pixels inside");
        check(px(f, 61, 57) == 0, "clip cuts pixels outside");
    }
    check(get_si("rgbFirst", "I") == (int32_t)0xff000000, "getRGB black");
    check(get_si("rgbSecond", "I") == (int32_t)0xffffffff, "getRGB white");

    /*--- keys: counter rect grows, repaint ---------------------------*/
    check(midp_key(-5, MIDP_KEY_DOWN) == FX_OK, "key delivery");
    check(midp_poll() == FX_OK, "poll after key");
    check(get_si("count", "I") == 1, "key reached the Canvas");
    check(get_si("paintCount", "I") == 2, "repaint after key");
    f = midp_frame();
    if (f)
        check(px(f, 32, 42) == 1, "counter rect after key");

    /*--- softkeys: Command dispatch through the listener -------------*/
    check(midp_softkey(0) == FX_OK, "softkey 0");
    check(get_si("commandFired", "I") == 1, "commandAction fired");
    check(str_is("commandLabel", "Go"), "command label delivered");
    check(midp_softkey(1) == FX_OK, "out-of-range softkey ignored");
    check(get_si("commandFired", "I") == 1, "no phantom command");

    /*--- two more presses destroy the MIDlet -------------------------*/
    check(midp_key(-5, MIDP_KEY_DOWN) == FX_OK, "key 2");
    check(midp_poll() == FX_OK, "poll 2");
    check(midp_key(-5, MIDP_KEY_DOWN) == FX_OK, "key 3");
    check(midp_poll() == FX_OK, "poll 3 (destroys)");
    check(get_si("count", "I") == 3, "three keys delivered");
    check(get_si("destroyed", "Z") == 1, "destroyApp ran");
    check(midp_finished(), "midp_finished set");
    check(midp_error()[0] == 0, "no runtime error");

    midp_stop();                       /* idempotent after destroy */
    check(get_si("destroyed", "Z") == 1, "no double destroy");

    if (failures) {
        printf("test_midp: %d failures\n", failures);
        return 1;
    }
    printf("test_midp: OK\n");
    return 0;
}
