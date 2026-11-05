/* fxj2me: VM heap — first-fit free list + non-moving mark-sweep GC.
 *
 * Objects are referenced by 32-bit heap offsets (jref), so host and target
 * behave identically. Roots are reported precisely: registered jref locations
 * and/or a root-scanner callback (the VM reports only slots it knows hold
 * references).
 */
#ifndef FXJ2ME_MEM_H
#define FXJ2ME_MEM_H

#include "fxj2me.h"

struct jclass;

#define JBLK_INUSE   0x1u
#define JBLK_ARRAY   0x2u
#define JBLK_REFELEM 0x4u   /* array elements are jrefs */
#define JBLK_MARK    0x18u  /* mark colour, in flag bits 3-4 */
#define JBLK_GRAY    (1u << 3)
#define JBLK_BLACK   (2u << 3)
/* array element size in flags bits 8-15 (1..255 bytes, 0 for objects) */
#define JBLK_ESIZE(b)     ((uint32_t)(((b)->flags >> 8) & 0xffu))
#define JBLK_WITH_ESIZE(f, e) \
    (((f) & 0xffu) | (((uint32_t)(e) & 0xffu) << 8))

/* Mark stack depth. A MIDlet's object graph is wide, not deep: 128 pending
 * refs is plenty, and an overflow is not fatal (heap_gc rescans until
 * drained), so the remaining 512 bytes go to classes and objects. */
#define JHEAP_MARK_STACK 128

/* Object header, placed immediately before the payload. Every object carries
 * one, and a MIDlet allocates hundreds of small ones, so it is kept to 16
 * bytes: the GC colour lives in the flag word and an array's element size is
 * implied by JBLK_REFELEM (refs are 4 bytes; primitive arrays are not
 * scanned). */
typedef struct jblk {
    uint32_t size;          /* total block size including header */
    uint32_t aux;           /* array length (arrays), else 0 */
    struct jclass *cls;     /* object class; links free list when free */
    uint32_t flags;
} jblk;

typedef struct jheap jheap;
typedef void (*jheap_root_fn)(jheap *h, void *ud);

struct jheap {
    uint8_t *base;
    size_t size;
    jref free_head;         /* offset of first free block's payload */

    jref *roots[64];        /* statically registered root locations */
    int nroots;
    jheap_root_fn root_fn;  /* dynamic root scanner (VM stacks, statics) */
    void *root_ud;

    jref mark_stack[JHEAP_MARK_STACK];
    int mark_top;
    int mark_overflow;

    int gc_count;
};

/* Create a heap over an existing memory block. */
int heap_create(jheap *h, void *block, size_t size);

/* Allocate a payload of `bytes` (rounded up to 8). Returns JREF_NULL if full. */
jref heap_alloc(jheap *h, size_t bytes, struct jclass *cls,
                uint32_t flags, uint32_t aux);

/* Pointer to payload (NULL for JREF_NULL). */
void *heap_ptr(jheap *h, jref r);

/* Header of the block containing payload r. */
jblk *heap_hdr(jheap *h, jref r);

void heap_free(jheap *h, jref r);

/* GC roots: every jref the VM can reach from statics / stacks. */
int heap_root_add(jheap *h, jref *loc);
void heap_set_root_scanner(jheap *h, jheap_root_fn fn, void *ud);

/* Used by root scanners: report a reachable reference. */
void heap_mark(jheap *h, jref r);

/* Mark from roots, then sweep. */
void heap_gc(jheap *h);

size_t heap_free_bytes(jheap *h);

#endif /* FXJ2ME_MEM_H */
