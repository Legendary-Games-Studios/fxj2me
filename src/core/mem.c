/* fxj2me: VM heap — first-fit free list + non-moving mark-sweep GC.
 *
 * A free block stores its free-list successor in the first payload word.
 * Frequent full coalescing (walk the heap) keeps the code simple; heaps are
 * at most a few hundred KB, so this is cheap compared to allocation rate.
 */
#include "classfile.h"
#include <string.h>

/* 4-byte alignment: every JVM value is a 32-bit slot (longs/doubles are two
 * slots, floats are soft-float), so nothing in an object needs 8. Rounding
 * payloads up to 8 wasted ~1k across the ~300 objects of a small MIDlet. */
#define ALIGN4(x) (((x) + 3u) & ~3u)
/* a free block only stores its free-list successor: one 4-byte jref */
#define MIN_PAYLOAD 4u

/* jref r is a payload offset from h->base; its header sits right before. */
static jblk *hdr_of(jheap *h, jref r)
{
    return (jblk *)(h->base + r) - 1;
}

static jref payload_ref(jheap *h, jblk *b)
{
    return (jref)((uint8_t *)(b + 1) - h->base);
}

static jref *free_next(jheap *h, jref r)
{
    return (jref *)(h->base + r);
}

int heap_create(jheap *h, void *block, size_t size)
{
    uintptr_t addr = ((uintptr_t)block + 3u) & ~(uintptr_t)3u;
    size_t lost = (size_t)(addr - (uintptr_t)block);
    jblk *b;

    memset(h, 0, sizeof *h);
    if (size < lost + sizeof(jblk) + ALIGN4(MIN_PAYLOAD))
        return FX_ERR_NOSPACE;
    h->base = (uint8_t *)addr;
    h->size = (size - lost) & ~(size_t)3u;

    b = (jblk *)h->base;
    b->size = (uint32_t)h->size;
    b->flags = 0;
    b->cls = NULL;
    b->aux = 0;
    *free_next(h, payload_ref(h, b)) = JREF_NULL;
    h->free_head = payload_ref(h, b);
    return FX_OK;
}

/* Rebuild the free list, merging adjacent free blocks. */
static void rebuild_freelist(jheap *h)
{
    size_t off = 0;
    jref last_free = JREF_NULL;

    h->free_head = JREF_NULL;
    while (off < h->size) {
        jblk *b = (jblk *)(h->base + off);
        if (!(b->flags & JBLK_INUSE) && b->size != 0) {
            while (off + b->size < h->size) {
                jblk *n = (jblk *)(h->base + off + b->size);
                if (n->flags & JBLK_INUSE)
                    break;
                b->size += n->size;
            }
            if (last_free == JREF_NULL)
                h->free_head = payload_ref(h, b);
            else
                *free_next(h, last_free) = payload_ref(h, b);
            last_free = payload_ref(h, b);
            *free_next(h, last_free) = JREF_NULL;
        }
        off += b->size;
    }
}

jref heap_alloc(jheap *h, size_t bytes, jclass *cls, uint32_t flags,
                uint32_t aux)
{
    size_t total = sizeof(jblk) + ALIGN4(bytes < MIN_PAYLOAD ? MIN_PAYLOAD
                                                             : bytes);
    jref prev = JREF_NULL, cur = h->free_head;

    while (cur != JREF_NULL) {
        jblk *b = hdr_of(h, cur);
        jref next = *free_next(h, cur);

        if (b->size >= total) {
            if (b->size - total >= sizeof(jblk) + ALIGN4(MIN_PAYLOAD)) {
                jblk *rest = (jblk *)((uint8_t *)b + total);
                rest->size = b->size - (uint32_t)total;
                rest->flags = 0;
                rest->cls = NULL;
                rest->aux = 0;
                b->size = (uint32_t)total;
                if (prev == JREF_NULL)
                    h->free_head = payload_ref(h, rest);
                else
                    *free_next(h, prev) = payload_ref(h, rest);
                *free_next(h, payload_ref(h, rest)) = next;
            } else {
                if (prev == JREF_NULL)
                    h->free_head = next;
                else
                    *free_next(h, prev) = next;
            }
            b->flags = JBLK_WITH_ESIZE(
                JBLK_INUSE | (flags & (JBLK_ARRAY | JBLK_REFELEM)),
                ((flags & JBLK_ARRAY) && aux) ? (uint32_t)(bytes / aux) : 0);
            b->cls = cls;
            b->aux = aux;
            return payload_ref(h, b);
        }
        prev = cur;
        cur = next;
    }
    return JREF_NULL;
}

void *heap_ptr(jheap *h, jref r)
{
    return r == JREF_NULL ? NULL : (void *)(h->base + r);
}

jblk *heap_hdr(jheap *h, jref r)
{
    return r == JREF_NULL ? NULL : hdr_of(h, r);
}

void heap_free(jheap *h, jref r)
{
    jblk *b;
    if (r == JREF_NULL)
        return;
    b = hdr_of(h, r);
    b->flags = 0;
    b->cls = NULL;
    b->aux = 0;
    rebuild_freelist(h);
}

int heap_root_add(jheap *h, jref *loc)
{
    if (h->nroots >= (int)(sizeof h->roots / sizeof h->roots[0]))
        return FX_ERR_NOSPACE;
    h->roots[h->nroots++] = loc;
    return FX_OK;
}

void heap_set_root_scanner(jheap *h, jheap_root_fn fn, void *ud)
{
    h->root_fn = fn;
    h->root_ud = ud;
}

/*--- mark-sweep --------------------------------------------------------*/

void heap_mark(jheap *h, jref r)
{
    jblk *b;
    /* Validate before dereferencing: refs are 4-aligned heap offsets. */
    if (r == JREF_NULL || (r & 3u) || (size_t)r + sizeof(jblk) > h->size)
        return;
    b = hdr_of(h, r);
    if (b->flags & JBLK_MARK)
        return;
    b->flags = (b->flags & ~JBLK_MARK) | JBLK_GRAY;   /* discovered */
    if (h->mark_top >= JHEAP_MARK_STACK) {
        h->mark_overflow = 1;       /* caller rescans until drained */
        return;
    }
    h->mark_stack[h->mark_top++] = r;
}

static void scan_children(jheap *h, jref r)
{
    jblk *b = hdr_of(h, r);
    uint32_t i;

    if (b->flags & JBLK_ARRAY) {
        if (b->flags & JBLK_REFELEM) {
            jref *elems = (jref *)(h->base + r);
            for (i = 0; i < b->aux; i++)
                heap_mark(h, elems[i]);
        }
    } else if (b->cls) {
        jclass *cls = b->cls;
        jslot *fields = (jslot *)(h->base + r);
        uint32_t n = cls->nslots;
        for (i = 0; i < n; i++)
            if ((cls->refmask[i / 32] >> (i % 32)) & 1)
                heap_mark(h, fields[i].r);
    }
    b->flags = (b->flags & ~JBLK_MARK) | JBLK_BLACK;  /* scanned */
}

void heap_gc(jheap *h)
{
    int i;

    do {
        h->mark_top = 0;
        h->mark_overflow = 0;
        for (i = 0; i < h->nroots; i++)
            heap_mark(h, *h->roots[i]);
        if (h->root_fn)
            h->root_fn(h, h->root_ud);
        while (h->mark_top > 0)
            scan_children(h, h->mark_stack[--h->mark_top]);
    } while (h->mark_overflow);

    /* sweep: free white blocks */
    {
        size_t off = 0;
        while (off < h->size) {
            jblk *b = (jblk *)(h->base + off);
            if ((b->flags & JBLK_INUSE) && !(b->flags & JBLK_MARK)) {
                b->flags = 0;
                b->cls = NULL;
                b->aux = 0;
            }
            b->flags &= ~JBLK_MARK;
            off += b->size;
        }
    }
    rebuild_freelist(h);
    h->gc_count++;
}

size_t heap_free_bytes(jheap *h)
{
    size_t total = 0;
    jref r = h->free_head;
    while (r != JREF_NULL) {
        total += hdr_of(h, r)->size;
        r = *free_next(h, r);
    }
    return total;
}
