/* fxj2me tests: heap allocation, free list and mark-sweep GC. */
#include "mem.h"
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

int main(void)
{
    static unsigned char block[256 * 1024];
    jheap h;
    jref a, b, c, arr;
    jref root = JREF_NULL;
    size_t free0, free1;

    check(heap_create(&h, block, sizeof block) == FX_OK, "heap_create");
    free0 = heap_free_bytes(&h);
    check(free0 > sizeof block / 2, "fresh heap is mostly free");

    /* plain allocations */
    a = heap_alloc(&h, 100, NULL, 0, 0);
    b = heap_alloc(&h, 200, NULL, 0, 0);
    c = heap_alloc(&h, 300, NULL, 0, 0);
    check(a && b && c, "allocations succeed");
    check(a != b && b != c, "allocations distinct");
    free1 = heap_free_bytes(&h);
    check(free1 < free0, "free space decreases");

    /* writing to one object must not disturb its neighbours */
    memset(heap_ptr(&h, a), 0xAA, 100);
    memset(heap_ptr(&h, c), 0x55, 300);
    check(((unsigned char *)heap_ptr(&h, b))[0] != 0xAA, "no overlap a/b");

    /* free + coalescing: free everything, space must come back */
    heap_free(&h, a);
    heap_free(&h, b);
    heap_free(&h, c);
    check(heap_free_bytes(&h) == free0, "coalescing restores space");

    /* GC: keep one object alive through a registered root */
    check(heap_root_add(&h, &root) == FX_OK, "root registration");
    root = heap_alloc(&h, 64, NULL, 0, 0);
    (void)heap_alloc(&h, 64, NULL, 0, 0);        /* garbage */
    (void)heap_alloc(&h, 64, NULL, 0, 0);        /* garbage */
    heap_gc(&h);
    check(root != JREF_NULL && heap_hdr(&h, root)->flags & JBLK_INUSE,
          "rooted object survives GC");
    check((heap_hdr(&h, root)->flags & JBLK_MARK) == 0,
          "marks cleared after GC");

    /* ref array keeps its elements alive */
    arr = heap_alloc(&h, 4 * sizeof(jref), NULL,
                     JBLK_ARRAY | JBLK_REFELEM, 4);
    check(arr != JREF_NULL, "ref array allocation");
    ((jref *)heap_ptr(&h, arr))[0] = root;
    ((jref *)heap_ptr(&h, arr))[1] = heap_alloc(&h, 32, NULL, 0, 0);
    ((jref *)heap_ptr(&h, arr))[2] = JREF_NULL;
    ((jref *)heap_ptr(&h, arr))[3] = JREF_NULL;
    root = arr;
    heap_gc(&h);
    check(heap_hdr(&h, ((jref *)heap_ptr(&h, arr))[1])->flags & JBLK_INUSE,
          "array-referenced object survives GC");

    /* allocation pressure: alloc + GC loop must not corrupt anything */
    {
        int i;
        for (i = 0; i < 20000; i++) {
            jref r = heap_alloc(&h, (size_t)(rand() % 500 + 8), NULL, 0, 0);
            if (r == JREF_NULL) {
                heap_gc(&h);
                r = heap_alloc(&h, (size_t)(rand() % 500 + 8), NULL, 0, 0);
            }
            check(r != JREF_NULL || heap_free_bytes(&h) < 600,
                  "alloc failure only when genuinely full");
            if (r == JREF_NULL)
                break;
        }
    }
    heap_gc(&h);
    check(heap_hdr(&h, arr)->flags & JBLK_INUSE, "root still alive after churn");

    if (failures) {
        printf("test_mem: %d failures\n", failures);
        return 1;
    }
    printf("test_mem: OK\n");
    return 0;
}
