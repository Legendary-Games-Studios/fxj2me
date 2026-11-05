/* fxj2me tests: shell core (config persistence, jar scan) over the host VFS
 * backend. Self-contained: builds its own fixture tree. Suite installation is
 * covered by test_ams. */
#include "fscfg.h"
#include "scan.h"
#include "vfs.h"

#include <stdio.h>
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

static void wf(const char *path, const char *content)
{
    vfs_file *f = vfs_open(path, 1);
    if (!f) {
        check(0, path);
        return;
    }
    vfs_write(f, content, (long)strlen(content));
    vfs_close(f);
}

static void test_paths(void)
{
    char buf[32];

    check(path_join(buf, sizeof buf, "/", "A") == 0 && strcmp(buf, "/A") == 0,
          "join root");
    check(path_join(buf, sizeof buf, "/GAMES", "X.JAR") == 0 &&
              strcmp(buf, "/GAMES/X.JAR") == 0,
          "join nested");
    check(path_join(buf, 8, "/GAMES", "X.JAR") == -1, "join overflow");
    check(strcmp(path_base("/A/B/C.JAR"), "C.JAR") == 0, "base nested");
    check(strcmp(path_base("C.JAR"), "C.JAR") == 0, "base plain");

    check(name_is_jar("GAME.JAR"), "jar upper");
    check(name_is_jar("game.jar"), "jar lower");
    check(name_is_jar("game.Jar"), "jar mixed");
    check(!name_is_jar("game.jarx"), "jar prefix no");
    check(!name_is_jar("jar"), "jar bare no");
    check(!name_is_jar("notes.txt"), "jar txt no");

    name_display(buf, sizeof buf, "GAME.JAR");
    check(strcmp(buf, "GAME") == 0, "display strips ext");
    name_display(buf, sizeof buf, "game.jar");
    check(strcmp(buf, "game") == 0, "display lower ext");
    name_display(buf, sizeof buf, "noext");
    check(strcmp(buf, "noext") == 0, "display no ext");
}

static void test_cfg(void)
{
    fxj2me_cfg c;
    char buf[256];
    const char *text;
    long n;

    cfg_defaults(&c);
    check(strcmp(c.suites_dir, "/FXJ2ME/SUITES") == 0, "default suites_dir");
    check(strcmp(c.scan_dir, "/") == 0, "default scan_dir");

    text = "# comment\n"
           "suites_dir=/GAMES\n"
           "scan_dir=/A/B\n"
           "junk line without equals\n"
           "suites_dir = /X\n"
           "scan_dir=../etc\n"
           "suites_dir=/BAD/\n"
           "\n";
    cfg_parse(&c, text, (long)strlen(text));
    check(strcmp(c.suites_dir, "/X") == 0, "parse: valid wins, invalid ignored");
    check(strcmp(c.scan_dir, "/A/B") == 0, "parse: relative path rejected");

    cfg_defaults(&c);
    cfg_parse(&c, "suites_dir=/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                  "AAAAAAAAAAAAAAAAAAAA\n",
              100);
    check(strcmp(c.suites_dir, "/FXJ2ME/SUITES") == 0, "parse: overlong rejected");

    cfg_defaults(&c);
    strcpy(c.suites_dir, "/GAMES");
    n = cfg_format(&c, buf, sizeof buf);
    check(n > 0, "format ok");
    {
        fxj2me_cfg d;
        cfg_parse(&d, buf, n);
        check(strcmp(d.suites_dir, c.suites_dir) == 0 &&
                  strcmp(d.scan_dir, c.scan_dir) == 0,
              "format/parse round trip");
    }
    check(cfg_format(&c, buf, 8) == -1, "format: small buffer rejected");

    /* persistence through the VFS */
    check(cfg_load(&c) == 1, "load: missing file -> defaults");
    cfg_defaults(&c);
    strcpy(c.suites_dir, "/GAMES");
    strcpy(c.scan_dir, "/L1");
    check(cfg_save(&c) == 0, "save");
    {
        fxj2me_cfg d;
        check(cfg_load(&d) == 0, "load after save");
        check(strcmp(d.suites_dir, "/GAMES") == 0 && strcmp(d.scan_dir, "/L1") == 0,
              "saved values persist");
    }
}

/*--- scan fixtures -----------------------------------------------------*/

static void build_tree(void)
{
    check(vfs_mkdir("/GAMES") == 0, "mkdir GAMES");
    check(vfs_mkdir("/GAMES/nested") == 0, "mkdir nested");
    wf("/GAMES/one.JAR", "ONE");
    wf("/GAMES/nested/two.jar", "TWO!!");
    wf("/GAMES/pic.png", "x");
    wf("/notes.txt", "hello");

    /* 10 levels deep: inside the scan depth */
    vfs_mkdir("/L1");  vfs_mkdir("/L1/L2");  vfs_mkdir("/L1/L2/L3");
    vfs_mkdir("/L1/L2/L3/L4");  vfs_mkdir("/L1/L2/L3/L4/L5");
    vfs_mkdir("/L1/L2/L3/L4/L5/L6");  vfs_mkdir("/L1/L2/L3/L4/L5/L6/L7");
    vfs_mkdir("/L1/L2/L3/L4/L5/L6/L7/L8");
    vfs_mkdir("/L1/L2/L3/L4/L5/L6/L7/L8/L9");
    vfs_mkdir("/L1/L2/L3/L4/L5/L6/L7/L8/L9/L10");
    wf("/L1/L2/L3/L4/L5/L6/L7/L8/L9/L10/three.JAR", "THREE");

    /* 11 levels deep: beyond SCAN_MAX_DEPTH */
    vfs_mkdir("/L1/L2/L3/L4/L5/L6/L7/L8/L9/L10/L11");
    wf("/L1/L2/L3/L4/L5/L6/L7/L8/L9/L10/L11/toodeep.JAR", "NOPE");
}

static int progress_count(const char *path, int found, void *ud)
{
    (void)path;
    (void)found;
    *(int *)ud += 1;
    return 0;
}

static int progress_cancel(const char *path, int found, void *ud)
{
    (void)path;
    (void)found;
    (void)ud;
    return 1;
}

static void test_scan(void)
{
    static jar_entry items[8];
    jar_list l;
    int calls = 0;

    jar_list_init(&l, items, 8);
    check(scan_jars("/", &l, progress_count, &calls) == 0, "scan completes");
    check(calls >= 3, "progress invoked");
    check(l.count == 3, "scan finds 3 jars");
    check(!l.truncated, "scan not truncated");
    if (l.count == 3) {
        check(strcmp(l.items[0].name, "one") == 0, "sorted 1 = one");
        check(strcmp(l.items[1].name, "three") == 0, "sorted 2 = three");
        check(strcmp(l.items[2].name, "two") == 0, "sorted 3 = two");
        check(l.items[0].size == 3, "size one.JAR");
        check(l.items[2].size == 5, "size two.jar");
        check(strcmp(l.items[2].path, "/GAMES/nested/two.jar") == 0,
              "full path recorded");
        check(strcmp(l.items[1].path,
                     "/L1/L2/L3/L4/L5/L6/L7/L8/L9/L10/three.JAR") == 0,
              "deep path recorded");
    }

    /* truncation */
    jar_list_init(&l, items, 1);
    check(scan_jars("/", &l, NULL, NULL) == 0, "scan cap completes");
    check(l.count == 1 && l.truncated, "scan cap truncates");

    /* cancellation */
    jar_list_init(&l, items, 8);
    check(scan_jars("/", &l, progress_cancel, NULL) == 1, "scan cancel");

    /* missing root is quiet */
    jar_list_init(&l, items, 8);
    check(scan_jars("/NOPE", &l, NULL, NULL) == 0, "scan missing root ok");
    check(l.count == 0, "scan missing root empty");
}

int main(void)
{
    char host_root[64], vfs_root[64];

    /* VFS paths are '/'-rooted; the host prefix must NOT start with './' or
     * "." + "./x" concatenates to "../x" and escapes the test root. */
    snprintf(vfs_root, sizeof vfs_root, "/shelltmp-%d", (int)getpid());
    snprintf(host_root, sizeof host_root, "shelltmp-%d", (int)getpid());

    hostvfs_set_root(".");
    check(vfs_mkdir(vfs_root) == 0, "create test root");
    hostvfs_set_root(host_root);

    test_paths();
    test_cfg();
    build_tree();
    test_scan();

    if (failures) {
        printf("test_shell: %d failures\n", failures);
        return 1;
    }
    printf("test_shell: OK\n");
    return 0;
}
