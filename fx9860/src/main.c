/* fxj2me — J2ME emulator shell for the fx-9750GIII.
 *
 * Phone-style application management: the shell lists installed MIDlet
 * suites (AMS records under the suites directory), installs a suite from a
 * jar plus its optional JAD descriptor, launches any MIDlet of a suite, and
 * uninstalls whole suites. See ams.h for the install rules.
 *
 * Memory: the stock OS maps only 32k of user RAM to add-ins and the linker
 * script exposes 6k statically; everything else is malloc()'d at runtime.
 * All lists are heap-allocated. */
#include <gint/clock.h>
#include <gint/display.h>
#include <gint/keyboard.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ams.h"
#include "fscfg.h"
#include "jar.h"
#include "manifest.h"
#include "midp.h"
#include "scan.h"
#include "selftest.h"
#include "vfs.h"
#include "vm.h"

/* The boot class library is prelinked into a const flash image at build time
 * (tools/gen_bootimg.c, romclass.h): the VM serves java and javax classes
 * straight out of it, so no boot jar is loaded or parsed at run time. */

#define MAX_ITEMS 48
#define PICK_MAX  32
#define ROWS      4
#define ROW_Y0    12
#define ROW_PITCH 10

enum {
    SCR_MENU, SCR_PLAY, SCR_SUITE, SCR_SCAN, SCR_SETTINGS, SCR_PICKER, SCR_MSG,
    SCR_QUIT
};

static fxj2me_cfg cfg;
static jar_entry *items;            /* scan results (install screen) */
static jar_list list;
static ams_suite *suites;           /* installed suite records */
static ams_list slist;
static int sel_suite;               /* suite index for SCR_SUITE */
static int pick_target;             /* cfg field the picker edits */
static char pick_cur[CFG_MAX_PATH];
static char pick_names[PICK_MAX][32];
static int pick_ndirs;
static char msg_title[24], msg_text[256];
static int msg_back;

/*--- drawing helpers ---------------------------------------------------*/

static void draw_title(const char *s)
{
    dprint(1, 1, C_BLACK, "%s", s);
}

static void draw_footer(const char *s)
{
    dprint(1, 55, C_BLACK, "%s", s);
}

static void draw_row(int y, const char *text, int selected)
{
    if (selected) {
        drect(0, y - 2, 127, y + 8, C_BLACK);
        dprint(2, y, C_WHITE, "%s", text);
    } else {
        dprint(2, y, C_BLACK, "%s", text);
    }
}

/* Keep the selected row inside the 4-row window. */
static void adj_top(int sel, int *top, int count)
{
    if (sel < *top)
        *top = sel;
    if (sel >= *top + ROWS)
        *top = sel - ROWS + 1;
    if (*top > count - ROWS)
        *top = count - ROWS;
    if (*top < 0)
        *top = 0;
}

static void draw_wrapped(const char *s, int y)
{
    char line[32];
    int len = 0;

    while (*s && y < 54) {
        int wl = 0;
        while (s[wl] && s[wl] != ' ')
            wl++;
        if (wl > 20)
            wl = 20;                    /* hard-break very long words */
        if (len > 0 && len + 1 + wl > 21) {
            line[len] = 0;
            dprint(1, y, C_BLACK, "%s", line);
            y += 10;
            len = 0;
        }
        if (len > 0)
            line[len++] = ' ';
        memcpy(line + len, s, (size_t)wl);
        len += wl;
        s += wl;
        while (*s == ' ')
            s++;
    }
    if (len > 0 && y < 54) {
        line[len] = 0;
        dprint(1, y, C_BLACK, "%s", line);
    }
}

static void set_msg(const char *title, const char *text, int back)
{
    snprintf(msg_title, sizeof msg_title, "%s", title);
    snprintf(msg_text, sizeof msg_text, "%s", text);
    msg_back = back;
}

/*--- path helpers ------------------------------------------------------*/

static void path_parent(char *dst, long cap, const char *path)
{
    long n = (long)strlen(path), i;
    if (n <= 1) {
        if (cap > 1) {
            dst[0] = '/';
            dst[1] = 0;
        }
        return;
    }
    i = n - 1;
    while (i > 0 && path[i] != '/')
        i--;
    if (i == 0) {
        dst[0] = '/';
        dst[1] = 0;
        return;
    }
    if (i >= cap)
        i = cap - 1;
    memcpy(dst, path, (size_t)i);
    dst[i] = 0;
}

/*--- MIDlet runner ----------------------------------------------------*/

/* Map calculator keys to MIDP key codes (Canvas.UP_KEY..SELECT_KEY and
 * phone keys KEY_NUM0..). F1/F6 stay reserved for the soft keys. */
static int map_key(int key)
{
    switch (key) {
    case KEY_UP:    return -1;
    case KEY_DOWN:  return -2;
    case KEY_LEFT:  return -3;
    case KEY_RIGHT: return -4;
    case KEY_EXE:   return -5;
    case KEY_0:     return 48;
    case KEY_1:     return 49;
    case KEY_2:     return 50;
    case KEY_3:     return 51;
    case KEY_4:     return 52;
    case KEY_5:     return 53;
    case KEY_6:     return 54;
    case KEY_7:     return 55;
    case KEY_8:     return 56;
    case KEY_9:     return 57;
    case KEY_LEFTP: return 35;   /* "(" -> '#' (flag/pound)  */
    case KEY_RIGHTP:return 42;   /* ")" -> '*' (star)        */
    case KEY_DEL:   return -8;   /* clear/backspace           */
    default:        return 0;
    }
}

/* Run one MIDlet of an installed suite. `path` is the suite's unpacked
 * package (pkg.h) and `info` the selected MIDlet with its merged (manifest +
 * descriptor) attributes. Class bytes are read from storage per class, so no
 * jar image is held in RAM. The frame occupies the whole screen; EXIT
 * destroys the MIDlet, F1..F6 fire its Commands in order. Returns SCR_SUITE
 * on exit, or SCR_MSG on failure. */
/* The scan and suite-record arrays only serve the list screens. Handing their
 * RAM to the VM during a launch matters far more than keeping them warm: at
 * ~11k they were several times the class-load headroom. */
static void drop_lists(void)
{
    free(items);
    items = NULL;
    free(suites);
    suites = NULL;
    slist.items = NULL;
    slist.count = 0;
    slist.cap = 0;
}

static int restore_lists(void)
{
    if (!items)
        items = malloc(MAX_ITEMS * sizeof *items);
    if (!suites)
        suites = malloc(MAX_ITEMS * sizeof *suites);
    return (items && suites) ? 0 : -1;
}

static int run_screen(const char *path, const midlet_info *info,
                      const char *name)
{
    void *java_heap = NULL;
    size_t heap_size = 0;
    vm *V = NULL;
    jthread *T = NULL;
    int next = SCR_SUITE;

    dclear(C_WHITE);
    draw_title("Run");
    dprint(1, 14, C_BLACK, "Loading...");
    {
        char nbuf[22];
        snprintf(nbuf, sizeof nbuf, "%s", name);
        dprint(1, 26, C_BLACK, "%s", nbuf);
    }
    dupdate();

    {
        int is_dir = 0;
        if (vfs_stat(path, &is_dir, NULL) != 0 || !is_dir) {
            set_msg("Run", "Suite package is missing.", SCR_SUITE);
            return SCR_MSG;
        }
    }
    drop_lists();             /* the VM gets every byte the lists were using */

    /* calloc: out: calls vm_free(V) even when vm_init never ran */
    V = calloc(1, sizeof *V);
    T = calloc(1, sizeof *T);
    if (!V || !T) {
        set_msg("Run", "Out of memory (VM).", SCR_SUITE);
        next = SCR_MSG;       /* failures must land on the message screen */
        goto out;
    }
    /* Only the VM structs are resident at this point; the probe measures
     * what the Java heap can have. */
    /* A MIDlet's own classes cost ~17k of metadata on the stock OS (pool,
     * bytecode, names, with boot-library text shared from flash); give class
     * loading that much and the rest to objects. */
    java_heap = probe_largest(&heap_size, 20 * 1024 + 512);
    if (!java_heap || heap_size < 4 * 1024) {
        set_msg("Run", "Out of memory (Java heap).", SCR_SUITE);
        next = SCR_MSG;       /* failures must land on the message screen */
        goto out;
    }

    if (vm_init(V, java_heap, heap_size, NULL, NULL) != FX_OK) {
        set_msg("Run", "VM init failed.", SCR_SUITE);
        next = SCR_MSG;       /* failures must land on the message screen */
        goto out;
    }
    snprintf(V->app_dir, sizeof V->app_dir, "%s", path);  /* classes from flash */
    memset(T, 0, sizeof *T);
    T->vm = V;
    V->thread = T;

    if (midp_start(V, T, info) != FX_OK) {
        set_msg("Run", midp_error(), SCR_SUITE);
        next = SCR_MSG;       /* failures must land on the message screen */
        goto out;
    }

    for (;;) {
        key_event_t ev = pollevent();
        int r = FX_OK;
        int code = map_key(ev.key);

        if (ev.type == KEYEV_DOWN) {
            if (ev.key == KEY_EXIT)
                break;
            if (ev.key >= KEY_F1 && ev.key <= KEY_F6)
                r = midp_softkey(ev.key - KEY_F1);
            else if (code)
                r = midp_key(code, MIDP_KEY_DOWN);
        } else if (ev.type == KEYEV_UP && code) {
            r = midp_key(code, MIDP_KEY_UP);
        }

        if (r == FX_OK)
            r = midp_poll();
        if (r != FX_OK) {
            set_msg("Run", midp_error(), SCR_SUITE);
            next = SCR_MSG;
            break;
        }
        if (midp_finished())
            break;

        memcpy(gint_vram, midp_frame(), MIDP_FRAME_SIZE);
        dupdate();
        sleep_ms(16);                  /* cap the pump at ~60 fps */
    }
    midp_stop();

out:
    if (V)
        vm_free(V);
    free(T);
    free(V);
    free(java_heap);
    if (restore_lists() != 0) {
        set_msg("Run", "Out of memory (lists).", SCR_QUIT);
        next = SCR_MSG;             /* cannot show the lists again */
    }
    return next;
}

/*--- screens -----------------------------------------------------------*/

static int menu_screen(void)
{
    int sel = 0;

    for (;;) {
        key_event_t ev;
        dclear(C_WHITE);
        draw_title("fxj2me");
        draw_row(ROW_Y0, "Play", sel == 0);
        draw_row(ROW_Y0 + ROW_PITCH, "Settings", sel == 1);
        draw_footer("[EXE] open");
        dupdate();

        ev = getkey();
        if (ev.key == KEY_UP || ev.key == KEY_DOWN)
            sel = 1 - sel;
        else if (ev.key == KEY_EXE)
            return sel == 0 ? SCR_PLAY : SCR_SETTINGS;
        else if (ev.key == KEY_EXIT)
            return SCR_QUIT;
    }
}

static void load_suites(void)
{
    ams_list_init(&slist, suites, MAX_ITEMS);
    ams_list_suites(cfg.suites_dir, &slist);
}

/* A phone installs a jar together with the descriptor beside it. */
static int find_jad(char *dst, long cap, const char *jar_path)
{
    static const char *const exts[2] = { ".jad", ".JAD" };
    long n = (long)strlen(jar_path);
    int i;

    if (n < 5 || n - 4 >= cap)
        return 0;
    memcpy(dst, jar_path, (size_t)(n - 4));
    for (i = 0; i < 2; i++) {
        snprintf(dst + n - 4, (size_t)(cap - (n - 4)), "%s", exts[i]);
        if (vfs_stat(dst, NULL, NULL) == 0)
            return 1;
    }
    return 0;
}

static int play_screen(void)
{
    int sel = 0, top = 0;

    load_suites();
    for (;;) {
        int total = slist.count + 1;    /* row 0: install new */
        int i;
        key_event_t ev;

        adj_top(sel, &top, total);
        dclear(C_WHITE);
        draw_title("Applications");
        for (i = 0; i < ROWS && top + i < total; i++) {
            int idx = top + i;
            char buf[24];
            if (idx == 0) {
                snprintf(buf, sizeof buf, "Install MIDlet suite...");
            } else {
                ams_suite *s = &suites[idx - 1];
                snprintf(buf, sizeof buf, "%.17s %.4s", s->name, s->version);
            }
            draw_row(ROW_Y0 + i * ROW_PITCH, buf, idx == sel);
        }
        draw_footer("[EXE] open [F1] install");
        dupdate();

        ev = getkey();
        if (ev.key == KEY_UP)
            sel = (sel + total - 1) % total;
        else if (ev.key == KEY_DOWN)
            sel = (sel + 1) % total;
        else if (ev.key == KEY_F1)
            return SCR_SCAN;
        else if (ev.key == KEY_EXE) {
            if (sel == 0)
                return SCR_SCAN;
            sel_suite = sel - 1;
            return SCR_SUITE;
        } else if (ev.key == KEY_EXIT)
            return SCR_MENU;
    }
}

/* One suite: its MIDlets (launch) plus uninstall of the whole suite. */
static int suite_screen(void)
{
    int sel = 0, top = 0;
    midlet_info info;
    ams_suite *s;

    if (sel_suite < 0 || sel_suite >= slist.count)
        return SCR_PLAY;
    s = &suites[sel_suite];
    if (ams_load_suite(cfg.suites_dir, s->id, &info) != FX_OK) {
        set_msg("Suite", "Suite record is damaged.", SCR_PLAY);
        return SCR_MSG;
    }

    for (;;) {
        int total = s->nmidlets + 1;    /* + uninstall */
        int i;
        key_event_t ev;

        adj_top(sel, &top, total);
        dclear(C_WHITE);
        draw_title(s->name);
        dprint(1, 11, C_BLACK, "%.10s %.6s %luk", s->vendor, s->version,
               (unsigned long)((s->jar_size + 1023) / 1024));
        for (i = 0; i < ROWS && top + i < total; i++) {
            int idx = top + i;
            int y = ROW_Y0 + 20 + i * ROW_PITCH;
            if (idx < s->nmidlets) {
                char buf[24];
                if (idx < info.nmidlets)
                    snprintf(buf, sizeof buf, "%s", info.midlets[idx].name);
                else
                    snprintf(buf, sizeof buf, "MIDlet %d", idx + 1);
                draw_row(y, buf, idx == sel);
            } else {
                draw_row(y, "Uninstall", idx == sel);
            }
        }
        draw_footer("[EXE] run [EXIT] back");
        dupdate();

        ev = getkey();
        if (ev.key == KEY_UP)
            sel = (sel + total - 1) % total;
        else if (ev.key == KEY_DOWN)
            sel = (sel + 1) % total;
        else if (ev.key == KEY_EXE) {
            if (sel < s->nmidlets) {
                char jar[96];
                /* midlet_select only touches name/icon/cls; midlets[] stays
                 * intact for the next redraw. */
                if (ams_load_midlet(cfg.suites_dir, s->id, sel, &info) != FX_OK) {
                    set_msg("Run", "Suite record is damaged.", SCR_PLAY);
                    return SCR_MSG;
                }
                if (ams_class_dir(jar, sizeof jar, cfg.suites_dir, s->id) != 0) {
                    set_msg("Run", "Suite path too long.", SCR_PLAY);
                    return SCR_MSG;
                }
                return run_screen(jar, &info, info.name);
            }
            ams_uninstall(cfg.suites_dir, s->id);
            return SCR_PLAY;            /* reloads the suite list */
        } else if (ev.key == KEY_EXIT) {
            return SCR_PLAY;
        }
    }
}

typedef struct {
    int ticks;
    int cancel;
} scan_ud;

static int scan_cb(const char *path, int found, void *ud)
{
    scan_ud *s = ud;
    key_event_t ev;

    (void)path;
    s->ticks++;
    if ((s->ticks & 7) == 0) {
        dclear(C_WHITE);
        draw_title("Scanning");
        dprint(1, 14, C_BLACK, "%s", cfg.scan_dir);
        dprint(1, 26, C_BLACK, "%d found", found);
        draw_footer("[EXIT] stop");
        dupdate();
    }

    ev = pollevent();
    if (ev.type == KEYEV_DOWN && ev.key == KEY_EXIT) {
        s->cancel = 1;
        return 1;
    }
    return 0;
}

static int scan_screen(void)
{
    int sel = 0, top = 0;

    for (;;) {
        scan_ud ud;
        int done = 0;

        jar_list_init(&list, items, MAX_ITEMS);
        ud.ticks = 0;
        ud.cancel = 0;

        dclear(C_WHITE);
        draw_title("Scanning");
        dprint(1, 14, C_BLACK, "%s", cfg.scan_dir);
        dupdate();

        scan_jars(cfg.scan_dir, &list, scan_cb, &ud);

        while (!done) {
            int i;
            key_event_t ev;

            adj_top(sel, &top, list.count);
            dclear(C_WHITE);
            if (list.count == 0)
                draw_title("No jars found");
            else if (list.truncated)
                draw_title("Found jars (max)");
            else
                draw_title("Found jars");
            for (i = 0; i < ROWS && top + i < list.count; i++) {
                jar_entry *e = &items[top + i];
                char buf[24];
                snprintf(buf, sizeof buf, "%.13s %luK", e->name,
                         (unsigned long)((e->size + 1023) / 1024));
                draw_row(ROW_Y0 + i * ROW_PITCH, buf, top + i == sel);
            }
            draw_footer("[EXE] install [F1] rescan");
            dupdate();

            ev = getkey();
            if (ev.key == KEY_UP && list.count > 0)
                sel = (sel + list.count - 1) % list.count;
            else if (ev.key == KEY_DOWN && list.count > 0)
                sel = (sel + 1) % list.count;
            else if (ev.key == KEY_F1)
                done = 1;               /* rescan */
            else if (ev.key == KEY_EXE && list.count > 0) {
                char jad[96], err[AMS_ERR_MAX];
                const char *jp = NULL;

                dclear(C_WHITE);
                draw_title("Installing");
                dprint(1, 14, C_BLACK, "%.20s", items[sel].name);
                dprint(1, 26, C_BLACK, "validating suite");
                dupdate();

                if (find_jad(jad, (long)sizeof jad, items[sel].path))
                    jp = jad;
                if (ams_install(items[sel].path, jp, cfg.suites_dir, NULL,
                                err, sizeof err) == FX_OK)
                    set_msg("Install", "Suite installed.", SCR_PLAY);
                else
                    set_msg("Install", err[0] ? err : "Install failed.",
                            SCR_SCAN);
                return SCR_MSG;
            } else if (ev.key == KEY_EXIT)
                return SCR_PLAY;
        }
    }
}

static int settings_screen(void)
{
    int sel = 0;

    for (;;) {
        key_event_t ev;
        dclear(C_WHITE);
        draw_title("Settings");
        draw_row(ROW_Y0, "Suites folder", sel == 0);
        draw_row(ROW_Y0 + ROW_PITCH, "Scan folder", sel == 1);
        draw_row(ROW_Y0 + 2 * ROW_PITCH, "VM self-test", sel == 2);
        if (sel == 0) {
            char buf[22];
            snprintf(buf, sizeof buf, "%s", cfg.suites_dir);
            dprint(1, 44, C_BLACK, "%s", buf);
        } else if (sel == 1) {
            char buf[22];
            snprintf(buf, sizeof buf, "%s", cfg.scan_dir);
            dprint(1, 44, C_BLACK, "%s", buf);
        }
        draw_footer("[EXE] change [EXIT] back");
        dupdate();

        ev = getkey();
        if (ev.key == KEY_UP)
            sel = (sel + 2) % 3;
        else if (ev.key == KEY_DOWN)
            sel = (sel + 1) % 3;
        else if (ev.key == KEY_EXE) {
            if (sel == 2) {
                selftest_run();
                continue;
            }
            pick_target = sel;
            snprintf(pick_cur, sizeof pick_cur, "%s",
                     sel == 0 ? cfg.suites_dir : cfg.scan_dir);
            return SCR_PICKER;
        } else if (ev.key == KEY_EXIT)
            return SCR_MENU;
    }
}

static void pick_load(void)
{
    vfs_dir *d;
    vfs_dirent e;

    pick_ndirs = 0;
    d = vfs_opendir(pick_cur);
    if (!d)
        return;
    while (pick_ndirs < PICK_MAX && vfs_readdir(d, &e) == 1) {
        if (e.name[0] == '.' &&
            (e.name[1] == 0 || (e.name[1] == '.' && e.name[2] == 0)))
            continue;
        if (!e.is_dir)
            continue;
        snprintf(pick_names[pick_ndirs], sizeof pick_names[0], "%s", e.name);
        pick_ndirs++;
    }
    vfs_closedir(d);
}

static int picker_screen(void)
{
    int sel = 0, top = 0;

    pick_load();
    for (;;) {
        int has_parent = strcmp(pick_cur, "/") != 0;
        int total = pick_ndirs + (has_parent ? 1 : 0);
        int i;
        char pbuf[22];
        key_event_t ev;

        adj_top(sel, &top, total);
        dclear(C_WHITE);
        draw_title(pick_target == 0 ? "Suites folder" : "Scan folder");
        for (i = 0; i < ROWS && top + i < total; i++) {
            int idx = top + i;
            if (has_parent && idx == 0)
                draw_row(ROW_Y0, "[..]", idx == sel);
            else {
                const char *nm =
                    pick_names[idx - (has_parent ? 1 : 0)];
                char buf[24];
                snprintf(buf, sizeof buf, "%s", nm);
                draw_row(ROW_Y0 + i * ROW_PITCH, buf, idx == sel);
            }
        }
        snprintf(pbuf, sizeof pbuf, "%s", pick_cur);
        dprint(1, 44, C_BLACK, "%s", pbuf);
        draw_footer("[F1] use here [EXE] open");
        dupdate();

        ev = getkey();
        if (ev.key == KEY_UP && total > 0)
            sel = (sel + total - 1) % total;
        else if (ev.key == KEY_DOWN && total > 0)
            sel = (sel + 1) % total;
        else if (ev.key == KEY_EXE && total > 0) {
            char next[CFG_MAX_PATH];
            if (has_parent && sel == 0) {
                path_parent(next, sizeof next, pick_cur);
                memcpy(pick_cur, next, sizeof pick_cur);
            } else {
                const char *nm = pick_names[sel - (has_parent ? 1 : 0)];
                if (path_join(next, sizeof next, pick_cur, nm) != 0)
                    continue;
                memcpy(pick_cur, next, sizeof pick_cur);
            }
            pick_load();
            sel = 0;
            top = 0;
        } else if (ev.key == KEY_F1) {
            char *dst = pick_target == 0 ? cfg.suites_dir : cfg.scan_dir;
            snprintf(dst, CFG_MAX_PATH, "%s", pick_cur);
            if (cfg_save(&cfg) == 0)
                set_msg("Settings", "Saved.", SCR_SETTINGS);
            else
                set_msg("Settings", "Could not save config.", SCR_SETTINGS);
            return SCR_MSG;
        } else if (ev.key == KEY_EXIT)
            return SCR_SETTINGS;
    }
}

static int msg_screen(void)
{
    for (;;) {
        dclear(C_WHITE);
        draw_title(msg_title);
        draw_wrapped(msg_text, 14);
        draw_footer("[EXIT] back");
        dupdate();
        if (getkey().key == KEY_EXIT)
            return msg_back;
    }
}

/*--- entry -------------------------------------------------------------*/

int main(void)
{
    int screen = SCR_MENU;

    cfg_load(&cfg);

    items = malloc(MAX_ITEMS * sizeof *items);
    suites = malloc(MAX_ITEMS * sizeof *suites);
    if (!items || !suites) {
        dclear(C_WHITE);
        draw_title("fxj2me");
        dprint(1, 14, C_BLACK, "out of memory");
        draw_footer("[EXIT] quit");
        dupdate();
        while (getkey().key != KEY_EXIT)
            ;
        return 1;
    }

    for (;;) {
        switch (screen) {
        case SCR_MENU:     screen = menu_screen(); break;
        case SCR_PLAY:     screen = play_screen(); break;
        case SCR_SUITE:    screen = suite_screen(); break;
        case SCR_SCAN:     screen = scan_screen(); break;
        case SCR_SETTINGS: screen = settings_screen(); break;
        case SCR_PICKER:   screen = picker_screen(); break;
        case SCR_MSG:      screen = msg_screen(); break;
        default:           free(items); free(suites); return 0;
        }
    }
}
