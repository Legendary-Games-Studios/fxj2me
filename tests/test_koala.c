/* fxj2me tests: third-party MIDlet smoke test.
 *
 * Drives KoalaMines 1.1 -- a real MIDP-1.0 game from 2006 (open source, see
 * tests/thirdparty/) -- through the whole runtime: launch (Canvas, Random,
 * Date, Form + TextField construction), gameplay keys, the options Form with
 * in-place editing, and modal Alerts.
 *
 * usage: test_koala <boot.jar> <KoalaMines.jar>
 * Skips (exit 77) when the jar is not present.
 */
#include "classfile.h"
#include "manifest.h"
#include "mem.h"
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

static int region_has_black(const uint8_t *f, int x0, int y0, int x1, int y1)
{
    int x, y;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            if ((f[y * (MIDP_FRAME_W / 8) + (x >> 3)] >> (7 - (x & 7))) & 1)
                return 1;
    return 0;
}

/* Runtime class of Display.getCurrent(), walking superclasses. */
static int current_isa(const char *name)
{
    jslot ret[2];
    jclass *c;

    if (vm_call_static(&V, &T, "javax/microedition/lcdui/Display", "getCurrent",
                       "()Ljavax/microedition/lcdui/Displayable;", NULL,
                       ret) != FX_OK || ret[0].r == JREF_NULL)
        return 0;
    for (c = heap_hdr(&V.heap, ret[0].r)->cls; c; c = c->super)
        if (c->name && strcmp(c->name, name) == 0)
            return 1;
    return 0;
}

/* Fire the current displayable's Command with the given label (F1..F6
 * semantics). Returns 1 when the command was found and fired. */
static int fire_command(const char *want)
{
    jslot args[2], ret[2];
    jref d, cmd;
    jclass *dc;
    size_t wlen = strlen(want);
    int n, i;

    if (vm_call_static(&V, &T, "javax/microedition/lcdui/Display", "getCurrent",
                       "()Ljavax/microedition/lcdui/Displayable;", NULL,
                       ret) != FX_OK || ret[0].r == JREF_NULL)
        return 0;
    d = ret[0].r;
    dc = heap_hdr(&V.heap, d)->cls;
    if (vm_call(&V, &T, dc, "commandCount", "()I", d, NULL, ret) != FX_OK)
        return 0;
    n = ret[0].i;
    for (i = 0; i < n; i++) {
        const uint16_t *chars;
        int len, k, ok;

        args[0].i = i;
        if (vm_call(&V, &T, dc, "commandAt",
                    "(I)Ljavax/microedition/lcdui/Command;", d, args,
                    ret) != FX_OK)
            return 0;
        cmd = ret[0].r;
        if (cmd == JREF_NULL)
            continue;
        if (vm_call(&V, &T, heap_hdr(&V.heap, cmd)->cls, "getLabel",
                    "()Ljava/lang/String;", cmd, NULL, ret) != FX_OK)
            return 0;
        if (vm_str_chars(&V, ret[0].r, &chars, &len) != FX_OK)
            return 0;
        ok = (len == (int)wlen);
        for (k = 0; ok && k < len; k++)
            if (chars[k] != (uint16_t)(unsigned char)want[k])
                ok = 0;
        if (ok)
            return midp_softkey(i) == FX_OK;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *boot_path = argc > 1 ? argv[1] : "boot.jar";
    const char *app_path = argc > 2 ? argv[2] : "KoalaMines.jar";
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

    /* a real MIDP-1.0 manifest from 2006 */
    check(manifest_parse(&app, &info) == FX_OK, "manifest_parse");
    check(strcmp(info.cls, "KoalaMines") == 0, "MIDlet-1 class");
    check(strcmp(info.name, "KoalaMines") == 0, "MIDlet-1 name");

    check(vm_init(&V, heap, sizeof heap, &boot, &app) == FX_OK, "vm_init");
    memset(&T, 0, sizeof T);
    T.vm = &V;
    V.thread = &T;

    /* Launch: startApp builds KoalaCanvas (new Random, new Date) and
     * KoalaOptions (extends Form, new TextField x4). */
    if (midp_start(&V, &T, &info) != FX_OK) {
        printf("FAIL: midp_start -> %s\n", midp_error());
        return 1;
    }
    check(midp_poll() == FX_OK, "first poll");
    check(current_isa("javax/microedition/lcdui/Canvas"), "game canvas shown");
    f = midp_frame();
    check(f != NULL && region_has_black(f, 0, 0, MIDP_FRAME_W, MIDP_FRAME_H),
          "grid painted");

    /* gameplay: diagonal moves + flag + open (any outcome is fine) */
    check(midp_key(49, MIDP_KEY_DOWN) == FX_OK, "key '1'");
    check(midp_key(57, MIDP_KEY_DOWN) == FX_OK, "key '9'");
    check(midp_key(-1, MIDP_KEY_DOWN) == FX_OK, "key up");
    check(midp_key(35, MIDP_KEY_DOWN) == FX_OK, "key '#' (flag)");
    check(midp_key(48, MIDP_KEY_DOWN) == FX_OK, "key '0' (open)");
    check(midp_poll() == FX_OK, "poll after keys");
    check(current_isa("javax/microedition/lcdui/Canvas"), "still in game");
    check(midp_error()[0] == 0, "no runtime error");

    /* Options: the Form with its TextFields */
    check(fire_command("Options"), "Options command fired");
    check(midp_poll() == FX_OK, "poll after Options");
    check(current_isa("javax/microedition/lcdui/Form"), "Form shown");
    f = midp_frame();
    check(f != NULL && region_has_black(f, 0, 0, MIDP_FRAME_W, MIDP_FRAME_H),
          "form painted");

    /* edit the selected TextField: EXE, digit, EXE commits */
    check(midp_key(-5, MIDP_KEY_DOWN) == FX_OK, "enter edit");
    check(midp_key(51, MIDP_KEY_DOWN) == FX_OK, "digit '3'");
    check(midp_key(-5, MIDP_KEY_DOWN) == FX_OK, "commit edit");
    check(midp_key(-2, MIDP_KEY_DOWN) == FX_OK, "move down");
    check(midp_poll() == FX_OK, "poll after edit");
    check(midp_error()[0] == 0, "no error while editing");

    /* Ok applies the values and returns to the game */
    check(fire_command("Ok"), "Ok command fired");
    check(midp_poll() == FX_OK, "poll after Ok");
    check(current_isa("javax/microedition/lcdui/Canvas"), "back on canvas");
    f = midp_frame();
    check(f != NULL && region_has_black(f, 0, 0, MIDP_FRAME_W, MIDP_FRAME_H),
          "grid repainted");

    /* Help: modal Alert (2-arg constructor), dismissed by any key */
    check(fire_command("Help"), "Help command fired");
    check(midp_poll() == FX_OK, "poll after Help");
    check(current_isa("javax/microedition/lcdui/Alert"), "Alert shown");
    f = midp_frame();
    check(f != NULL && region_has_black(f, 0, 0, MIDP_FRAME_W, MIDP_FRAME_H),
          "alert painted");
    check(midp_key(-1, MIDP_KEY_DOWN) == FX_OK, "dismiss key");
    check(midp_poll() == FX_OK, "poll after dismiss");
    check(current_isa("javax/microedition/lcdui/Canvas"), "alert dismissed");

    /* Quit: destroyApp path */
    check(fire_command("Quit"), "Quit command fired");
    check(midp_poll() == FX_OK, "poll after Quit");
    check(midp_finished(), "midp_finished set");
    check(midp_error()[0] == 0, "no error on exit");
    midp_stop();

    if (failures) {
        printf("test_koala: %d failures\n", failures);
        return 1;
    }
    printf("test_koala: OK\n");
    return 0;
}
