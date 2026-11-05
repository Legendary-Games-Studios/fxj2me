/* fxj2me tests: string + exception semantics against the boot class
 * library. Fixture: tests/fixtures/Runtime.java compiled into test.jar.
 *
 * usage: test_runtime <boot.jar> <test.jar>
 */
#include "classfile.h"
#include "romclass.h"
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

/* Call a static method on the Runtime fixture. Returns the VM status. */
static int call(const char *m, const char *desc, jslot *ret)
{
    jslot r[2];
    r[0].i = 0;
    r[1].i = 0;
    return vm_call_static(&V, &T, "Runtime", m, desc, NULL,
                          ret ? ret : r);
}

static int32_t call_i(const char *m, const char *desc, const char *what)
{
    jslot ret[2];
    if (call(m, desc, ret) != FX_OK) {
        printf("FAIL: %s -> %s\n", what,
               T.error ? T.error : "(no error text)");
        failures++;
        return 0;
    }
    return ret[0].i;
}

static void check_str(const char *m, const char *want)
{
    jslot ret[2];
    if (call(m, "()Ljava/lang/String;", ret) != FX_OK) {
        printf("FAIL: %s -> %s\n", m, T.error ? T.error : "?");
        failures++;
        return;
    }
    {
        const uint16_t *chars;
        int len = 0, i, ok;
        if (vm_str_chars(&V, ret[0].r, &chars, &len) != FX_OK) {
            printf("FAIL: %s (unreadable string)\n", m);
            failures++;
            return;
        }
        ok = (len == (int)strlen(want));
        for (i = 0; ok && i < len; i++)
            if (chars[i] != (uint16_t)(unsigned char)want[i])
                ok = 0;
        if (!ok) {
            printf("FAIL: %s (got %d chars, want \"%s\")\n", m, len, want);
            failures++;
        }
    }
}

int main(int argc, char **argv)
{
    const char *boot_path = argc > 1 ? argv[1] : "boot.jar";
    const char *app_path = argc > 2 ? argv[2] : "test.jar";
    static unsigned char heap[2 * 1024 * 1024];
    unsigned char *boot_blob, *app_blob;
    size_t boot_len = 0, app_len = 0;
    jar boot, app;

    boot_blob = read_file(boot_path, &boot_len);
    app_blob = read_file(app_path, &app_len);
    if (!boot_blob || !app_blob) {
        printf("SKIP: missing %s / %s\n", boot_path, app_path);
        return 77;
    }
    check(jar_open(&boot, boot_blob, boot_len) == FX_OK, "jar_open boot");
    check(jar_open(&app, app_blob, app_len) == FX_OK, "jar_open app");
    check(vm_init(&V, heap, sizeof heap, &boot, &app) == FX_OK, "vm_init");
    memset(&T, 0, sizeof T);
    T.vm = &V;
    V.thread = &T;

    /*--- boot ROM image ---------------------------------------------*/
    {
        /* The boot library must come from the build-time flash image, not
         * from a parsed jar: that is where the launch's RAM saving lives. */
        jclass *obj = vm_load_class(&V, "java/lang/Object");
        check(obj && obj->rom, "boot classes load from the ROM image");
        check(V.nclasses >= 1 && vm_load_class(&V, "java/lang/Object") == obj,
              "ROM class is cached");
        check(class_find_method(obj, "<init>", "()V") != NULL,
              "ROM class has its methods");
    }
    /*--- shared pool text --------------------------------------------*/
    if (fx_rom_nstrs) {
        /* Parsed classes must point at the image's copy of text the boot
         * library already holds instead of malloc\'ing their own. */
        jclass *app = vm_load_class(&V, "Runtime");
        int shared = 0, k;
        for (k = 0; app && k < app->nmethods; k++) {
            const char *n = app->methods[k].name;
            if (n && rom_intern(n) == n)
                shared++;
        }
        check(shared > 0, "app pool text is shared from the ROM image");
        check(rom_intern("javax/microedition/lcdui/Graphics") != NULL,
              "image carries the MIDP class names");
    }

    /*--- strings ----------------------------------------------------*/
    check(call_i("strLen", "()I", "strLen") == 6, "String.length");
    check(call_i("strAt", "()C", "strAt") == 'j', "String.charAt");
    check(call_i("strEq", "()Z", "strEq") == 1, "String.equals");
    check(call_i("interned", "()Z", "interned") == 1,
          "literal interning identity");
    check_str("lit", "fxj2me");
    check_str("strCat", "n=7!");
    check_str("strSub", "cde");
    check_str("unparse", "-17");
    check(call_i("parse", "()I", "parse") == 42, "Integer.parseInt");

    /*--- exceptions -------------------------------------------------*/
    check(call_i("catchAIOOBE", "()I", "catchAIOOBE") == 1,
          "catch implicit ArrayIndexOutOfBounds");
    check(call_i("catchDiv0", "()I", "catchDiv0") == 2,
          "catch implicit ArithmeticException");
    check(call_i("catchThrown", "()I", "catchThrown") == 3,
          "catch thrown exception");
    check(call_i("catchSuper", "()I", "catchSuper") == 4,
          "catch by superclass");
    check(call_i("nestedUnwind", "()I", "nestedUnwind") == 5,
          "unwind across frames to handler");
    check(call_i("rethrow", "()I", "rethrow") == 6,
          "rethrow replaces pending exception");
    check(call_i("catchNegArray", "()I", "catchNegArray") == 7,
          "catch implicit NegativeArraySize");
    check(call_i("catchNPE", "()I", "catchNPE") == 8,
          "catch implicit NullPointerException");
    check(call_i("finallyOrder", "()I", "finallyOrder") == 1110,
          "finally ordering");
    check_str("caughtMsg", "boom");

    /* uncaught: status + formatted error text */
    {
        jslot ret[2];
        check(call("uncaught", "()Ljava/lang/String;", ret) == FX_ERR_JAVA,
              "uncaught throw returns FX_ERR_JAVA");
        check(T.error && strstr(T.error, "propagate") != NULL,
              "error text carries the message");
        check(T.error && strstr(T.error, "IllegalArgumentException") != NULL,
              "error text carries the class");
    }
    {
        jslot ret[2];
        check(call("nullDeref", "()I", ret) == FX_ERR_JAVA,
              "uncaught implicit NPE returns FX_ERR_JAVA");
    }

    if (failures) {
        printf("test_runtime: %d failures\n", failures);
        return 1;
    }
    printf("test_runtime: OK\n");
    return 0;
}
