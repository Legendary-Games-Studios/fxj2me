/* fxj2me device: VM bring-up self-test.
 *
 * Boots the VM core against the embedded test jar and runs the same
 * interpreter battery as the host suite. All allocations (VM, thread, Java
 * heap, class metadata) are released before returning to the shell. */
#include <gint/display.h>
#include <gint/keyboard.h>

#include <stdlib.h>
#include <string.h>
#include "vm.h"

#include "selftest.h"

extern const unsigned char fxj2me_test_jar[];
extern const unsigned int fxj2me_test_jar_len;

static int n_pass, n_fail;
static const char *fails[6];

static void record(int ok, const char *name)
{
    if (ok) {
        n_pass++;
    } else {
        if (n_fail < 6)
            fails[n_fail] = name;
        n_fail++;
    }
}

static int call_i(vm *V, jthread *T, const char *m, const char *desc,
                  const jslot *args, int32_t *out)
{
    jslot ret[2];
    ret[0].i = 0;
    ret[1].i = 0;
    if (vm_call_static(V, T, "Fib", m, desc, args, ret) != FX_OK)
        return 0;
    *out = ret[0].i;
    return 1;
}

static void expect_i(vm *V, jthread *T, const char *name, const char *m,
                     const char *desc, jslot *args, int32_t want)
{
    int32_t got = 0;
    record(call_i(V, T, m, desc, args, &got) && got == want, name);
}

static void run_selftests(vm *V, jthread *T)
{
    jslot args[8], ret[2];
    double derr;

    memset(args, 0, sizeof args);

    args[0].i = 15;
    expect_i(V, T, "fib", "fib", "(I)I", args, 610);
    args[0].i = 100;
    expect_i(V, T, "sumLoop", "sumLoop", "(I)I", args, 5050);
    args[0].i = 10;
    expect_i(V, T, "arraySum", "arraySum", "(I)I", args, 285);
    args[0].i = 4;
    expect_i(V, T, "matrix", "matrix", "(I)I", args, 36);
    args[0].i = 5;
    expect_i(V, T, "switchy", "switchy", "(I)I", args, 500);
    args[0].i = -7;
    expect_i(V, T, "lookupSw", "lookupSwitchy", "(I)I", args, 7);
    args[0].i = 7;
    args[1].i = 3;
    expect_i(V, T, "arithmetic", "arithmetic", "(II)I", args, 56);

    args[0].i = 20;
    ret[0].i = ret[1].i = 0;
    record(vm_call_static(V, T, "Fib", "factLong", "(I)J", args, ret)
               == FX_OK && jslot_get_long(ret) == 2432902008176640000LL,
           "factLong");

    args[0].f = 2.0f;
    ret[0].i = ret[1].i = 0;
    record(vm_call_static(V, T, "Fib", "floats", "(F)F", args, ret) == FX_OK
               && ret[0].f == 6.0f,
           "floats");

    jslot_set_double(args, 1.0);
    args[2].i = 4;
    ret[0].i = ret[1].i = 0;
    derr = 0.0;
    if (vm_call_static(V, T, "Fib", "doubles", "(DI)D", args, ret) == FX_OK)
        derr = jslot_get_double(ret) - 2.0833333333333335;
    record(derr < 1e-9 && derr > -1e-9, "doubles");

    args[0].i = 5;
    expect_i(V, T, "statics1", "statics", "(I)I", args, 5);
    args[0].i = 7;
    expect_i(V, T, "statics2", "statics", "(I)I", args, 12);

    args[0].i = 4;
    expect_i(V, T, "objects", "objects", "(I)I", args, 10);

    jslot_set_long(args, 100);
    args[2].i = 3;
    expect_i(V, T, "wideArgs", "wideArgs", "(JI)I", args, 381);

    jslot_set_long(args, 10);
    jslot_set_double(&args[2], 2.0);
    args[4].i = 3;
    expect_i(V, T, "mixes", "mixes", "(JDI)I", args, 18);
}

static void fail_screen(const char *msg)
{
    dclear(C_WHITE);
    dprint(1, 1, C_BLACK, "fxj2me VM selftest");
    dprint(1, 14, C_BLACK, "%s", msg);
    dprint(1, 55, C_BLACK, "[EXIT] back");
    dupdate();
    while (getkey().key != KEY_EXIT)
        ;
}

/* The stock OS heap is far smaller than the hardware's 512k of RAM, so the
 * Java heap size is discovered at boot: find the largest block malloc() can
 * still give us (2k-granular, 4k..384k). The final block is split between the
 * Java heap and class-load headroom — the class metadata vm_load_class
 * allocates for the MIDlet's own classes — without which the probe would
 * starve class loading.
 *
 * `class_reserve` is how much the caller knows its class loading can need; a
 * MIDlet's own classes cost ~16k on the stock OS (cp, bytecode, names, with
 * boot-library text shared from flash), while the self-test battery only
 * needs a couple of k. The reserve is at least a third of the block (big
 * arenas: objects are the bulk) but never more than half, so tiny arenas keep
 * the even split.
 *
 * Each probe is freed immediately: holding the candidate while probing larger
 * sizes halves the free space and makes the result depend on the search path
 * (that bug shrank the measured heap from 26k to 18k). */
void *probe_largest(size_t *out, size_t class_reserve)
{
    size_t lo = 4 * 1024, hi = 384 * 1024, best = 0;
    void *p = NULL;
    while (lo <= hi) {
        size_t mid = lo + (hi - lo) / 2;
        void *t = malloc(mid);
        if (t) {
            free(t);            /* measure, don't hold */
            best = mid;
            lo = mid + 2048;
        } else {
            hi = mid - 2048;    /* mid >= lo >= 4k: no underflow */
        }
    }
    if (best) {
        size_t want, reserve = best / 3;
        if (reserve < class_reserve)
            reserve = class_reserve;
        if (reserve > best / 2)
            reserve = best / 2;         /* never starve the objects */
        want = best - reserve;
        if (want < 12 * 1024 && best > 16 * 1024)
            want = 12 * 1024;       /* never hand out a thrashing heap */
        p = malloc(want);
        best = p ? want : 0;
    }
    *out = best;
    return p;
}

void selftest_run(void)
{
    vm *V;
    jthread *T;
    jar J;
    void *java_heap;
    size_t heap_size = 0;
    int i, row;

    n_pass = n_fail = 0;

    dclear(C_WHITE);
    dprint(1, 1, C_BLACK, "fxj2me VM selftest");
    dprint(1, 14, C_BLACK, "booting VM...");
    dupdate();

    /* All big allocations come from malloc: static RAM is only 6k. Take the
     * small structures first, then give everything that's left to the Java
     * heap so its size is discovered instead of assumed. */
    V = malloc(sizeof *V);
    T = malloc(sizeof *T);
    java_heap = probe_largest(&heap_size, 8 * 1024);
    if (!V || !T) {
        fail_screen("out of memory (vm)");
        goto out;
    }
    if (!java_heap) {
        fail_screen("out of memory (heap)");
        goto out;
    }
    if (jar_open(&J, fxj2me_test_jar, fxj2me_test_jar_len) != FX_OK) {
        fail_screen("jar open failed");
        goto out;
    }
    /* No boot class library: the battery only needs the interpreter. */
    if (vm_init(V, java_heap, heap_size, NULL, &J) != FX_OK) {
        fail_screen("vm init failed");
        jar_close(&J);
        goto out;
    }
    memset(T, 0, sizeof *T);
    T->vm = V;
    V->thread = T;

    run_selftests(V, T);

    dclear(C_WHITE);
    dprint(1, 1, C_BLACK, "fxj2me VM selftest");
    row = 14;
    dprint(1, row, C_BLACK, "%d/%d passed", n_pass, n_pass + n_fail);
    row += 10;
    if (n_fail == 0) {
        dprint(1, row, C_BLACK, "ALL TESTS PASSED");
    } else {
        dprint(1, row, C_BLACK, "failed:");
        row += 10;
        for (i = 0; i < n_fail && i < 4 && row < 55; i++, row += 10)
            dprint(8, row, C_BLACK, "%s", fails[i]);
    }
    dprint(1, 55, C_BLACK, "[EXIT] back");
    dprint(68, 55, C_BLACK, "heap %uk", (unsigned)(heap_size >> 10));
    dupdate();

    while (getkey().key != KEY_EXIT)
        ;

    vm_free(V);
    jar_close(&J);
out:
    free(java_heap);
    free(V);
    free(T);
}
