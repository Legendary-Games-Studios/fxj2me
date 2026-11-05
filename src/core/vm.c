/* fxj2me: bytecode interpreter. Switch dispatch; computed-goto later.
 *
 * Slot types are tracked in shadow maps so the GC root scan is precise:
 *   0 = primitive cat1 / wide low word, 1 = reference, 2 = wide high word.
 */
#include "vm.h"
#include "natives.h"
#include "romclass.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*--- big-endian readers ------------------------------------------------*/

static inline uint16_t rd_u2(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline int32_t rd_s4(const uint8_t *p)
{
    return (int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                     ((uint32_t)p[2] << 8) | (uint32_t)p[3]);
}

/*--- builtin classes ---------------------------------------------------*/

static jclass *make_builtin(const char *name)
{
    jclass *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->name = malloc(strlen(name) + 1);
    if (!c->name) {
        free(c);
        return NULL;
    }
    strcpy(c->name, name);
    c->refmask = calloc(1, sizeof(uint32_t));
    c->srefmask = calloc(1, sizeof(uint32_t));
    c->static_vals = calloc(1, sizeof(jslot));
    c->linked = true;
    c->initialized = true;
    return c;
}

static jclass *resolve_cb(const char *name, void *ud)
{
    return vm_load_class((vm *)ud, name);
}

/* Extract <name>.class from one jar on the classpath. */
static int class_from_jar(jar *j, const char *ename, uint8_t **buf,
                          size_t *len)
{
    int idx;
    if (!j)
        return FX_ERR_NOTFOUND;
    idx = jar_find(j, ename);
    if (idx < 0)
        return FX_ERR_NOTFOUND;
    *len = j->entries[idx].usize;
    *buf = malloc(*len ? *len : 1);
    if (!*buf)
        return FX_ERR_NOSPACE;
    if (jar_extract(j, idx, *buf, *len, NULL) != FX_OK) {
        free(*buf);
        *buf = NULL;
        return FX_ERR_FORMAT;
    }
    return FX_OK;
}

/* Read one class out of the installed-suite package: it is its own file in
 * storage, so only this class's copy is allocated and no jar image (nor any
 * seek into one) is involved. */
static int class_from_pkg(const char *dir, const char *ename, uint8_t **buf,
                          size_t *len)
{
    return pkg_extract(dir, ename, buf, len);
}

jclass *vm_load_class(vm *v, const char *name)
{
    jclass *cl = NULL;
    uint8_t *buf = NULL;
    size_t len = 0;
    char ename[288];
    int i;

    for (i = 0; i < v->nclasses; i++)
        if (strcmp(v->classes[i]->name, name) == 0)
            return v->classes[i];

    /* Boot class library: served from the flash image when the name is in
     * it, so no parsing and no RAM beyond the header plus statics. */
    i = rom_find(name);
    if (i >= 0) {
        cl = class_from_rom(i);
        if (!cl) {
            snprintf(v->errbuf, sizeof v->errbuf,
                     "out of memory loading class: %.110s", name);
            return NULL;
        }
        v->errbuf[0] = 0;
        if (class_link(cl, resolve_cb, v) != FX_OK) {
            if (!v->errbuf[0])
                snprintf(v->errbuf, sizeof v->errbuf, "cannot link: %.120s",
                         name);
            class_free(cl);
            return NULL;
        }
        if (v->nclasses >= VM_MAX_CLASSES) {
            snprintf(v->errbuf, sizeof v->errbuf, "too many classes: %.120s",
                     name);
            class_free(cl);
            return NULL;
        }
        v->classes[v->nclasses++] = cl;
        return cl;
    }
    if (strlen(name) + 7 >= sizeof ename) {
        snprintf(v->errbuf, sizeof v->errbuf, "class name too long: %.100s",
                 name);
        return NULL;
    }
    strcpy(ename, name);
    strcat(ename, ".class");

    /* Classpath order: boot class library first, then the app jar. A class
     * that is listed but cannot be read (corrupt entry, out of memory) must
     * say so — "missing" alone sends debugging down the wrong path. */
    {
        int rb = class_from_jar(v->boot, ename, &buf, &len);
        int ra = FX_ERR_NOTFOUND;

        if (rb != FX_OK && v->app_dir[0])
            ra = class_from_pkg(v->app_dir, ename, &buf, &len);
        if (rb != FX_OK && ra == FX_ERR_NOTFOUND)
            ra = class_from_jar(v->app, ename, &buf, &len);
        if (rb != FX_OK && ra != FX_OK) {
            int r = ra != FX_ERR_NOTFOUND ? ra : rb;
            if (r == FX_ERR_NOSPACE)
                snprintf(v->errbuf, sizeof v->errbuf,
                         "out of memory loading class: %.110s", name);
            else if (r == FX_ERR_FORMAT)
                snprintf(v->errbuf, sizeof v->errbuf,
                         "unreadable class entry (bad jar?): %.110s", name);
            else
                snprintf(v->errbuf, sizeof v->errbuf, "missing class: %.120s",
                         name);
            return NULL;
        }
    }
    {
        int rp = class_parse(buf, len, &cl);
        if (rp != FX_OK) {
            if (rp == FX_ERR_NOSPACE)
                snprintf(v->errbuf, sizeof v->errbuf,
                         "out of memory loading class: %.110s", name);
            else
                snprintf(v->errbuf, sizeof v->errbuf,
                         "bad class file: %.120s", name);
            return NULL;                   /* class_parse freed buf */
        }
    }
    v->errbuf[0] = 0;                      /* fresh chain for link errors */
    if (class_link(cl, resolve_cb, v) != FX_OK) {
        /* A failed resolve already named the missing super/interface. */
        if (!v->errbuf[0])
            snprintf(v->errbuf, sizeof v->errbuf, "cannot link: %.120s", name);
        class_free(cl);
        return NULL;
    }
    if (v->nclasses >= VM_MAX_CLASSES) {
        snprintf(v->errbuf, sizeof v->errbuf, "too many classes: %.120s",
                 name);
        class_free(cl);
        return NULL;
    }
    v->classes[v->nclasses++] = cl;
    return cl;
}

void vm_free(vm *v)
{
    int i;
    if (!v)
        return;
    for (i = 0; i < v->nclasses; i++)
        class_free(v->classes[i]);
    v->nclasses = 0;
    v->ninterns = 0;
    v->thread = NULL;
}

/*--- GC roots ----------------------------------------------------------*/

static void vm_mark_roots(jheap *h, void *ud)
{
    vm *v = (vm *)ud;
    jthread *t = v->thread;
    int i, j;

    /* Stale typed slots past sp/ltop may keep garbage alive; that is safe. */
    if (t) {
        for (i = 0; i < VM_STACK_SLOTS; i++)
            if (t->ttypes[i] == 1)
                heap_mark(h, t->stack[i].r);
        for (i = 0; i < VM_LOCALS_SLOTS; i++)
            if (t->ltypes[i] == 1)
                heap_mark(h, t->locals[i].r);
    }
    for (i = 0; i < v->nclasses; i++) {
        jclass *c = v->classes[i];
        for (j = 0; j < (int)c->nstat_slots; j++)
            if ((c->srefmask[j / 32] >> (j % 32)) & 1)
                heap_mark(h, c->static_vals[j].r);
        if (c->mirror)
            heap_mark(h, c->mirror);
    }
    for (i = 0; i < v->ninterns; i++)
        heap_mark(h, v->interns[i]);
    heap_mark(h, v->tmp[0]);
    heap_mark(h, v->tmp[1]);
    if (v->thread)
        heap_mark(h, v->thread->exception);
}

jref vm_alloc(vm *v, size_t bytes, jclass *cls, uint32_t flags,
              uint32_t aux)
{
    jref r = heap_alloc(&v->heap, bytes, cls, flags, aux);
    if (r == JREF_NULL) {
        heap_gc(&v->heap);
        r = heap_alloc(&v->heap, bytes, cls, flags, aux);
    }
    if (r != JREF_NULL)
        memset(heap_ptr(&v->heap, r), 0, bytes);   /* JVM: zero-initialized */
    return r;
}

/* java/lang/Object.<init> when no class library is present; everything
 * else goes through the native registry (natives.c). */
static int run_native(vm *v, jthread *t, jclass *oc, const char *mn,
                      const char *md, const jslot *args, jslot *ret)
{
    if (strcmp(mn, "<init>") == 0 &&
        strcmp(oc->name, "java/lang/Object") == 0 &&
        !v->boot) {
        ret[0].i = 0;
        ret[1].i = 0;
        return FX_OK;
    }
    return natives_call(v, t, oc, mn, md, args, ret);
}

int vm_init(vm *v, void *heap_block, size_t heap_size, jar *boot, jar *app)
{
    jclass *obj;

    memset(v, 0, sizeof *v);
    if (heap_create(&v->heap, heap_block, heap_size) != FX_OK)
        return FX_ERR_NOSPACE;
    v->boot = boot;
    v->app = app;

    /* Without a boot class library (core-level tests) provide a builtin
     * Object; with one, java/lang/Object comes from boot.jar. */
    if (!boot) {
        obj = make_builtin("java/lang/Object");
        if (!obj)
            return FX_ERR_NOSPACE;
        v->classes[v->nclasses++] = obj;
    }

    heap_set_root_scanner(&v->heap, vm_mark_roots, v);
    return FX_OK;
}

/*--- strings ----------------------------------------------------------*/

/* String layout contract (see lib/java/java/lang/String.java):
 * instance field "value" ([C) holds UTF-16 chars, "count" (I) their count. */
static int string_slots(vm *v, uint32_t *val_slot, uint32_t *cnt_slot)
{
    jclass *s = vm_load_class(v, "java/lang/String");
    bool is_ref;
    if (!s)
        return FX_ERR_NOTFOUND;
    if (class_find_field(s, "value", "[C", false, val_slot, &is_ref) != FX_OK)
        return FX_ERR_NOTFOUND;
    return class_find_field(s, "count", "I", false, cnt_slot, NULL);
}

jref vm_make_string16(vm *v, const uint16_t *chars, int len)
{
    uint32_t vslot, cslot;
    jclass *s;
    jref arr, obj;
    jslot *f;
    uint16_t *p;
    int i;

    if (len < 0)
        return JREF_NULL;
    s = vm_load_class(v, "java/lang/String");
    if (!s || string_slots(v, &vslot, &cslot) != FX_OK)
        return JREF_NULL;
    arr = vm_alloc(v, (size_t)(len ? len : 1) * 2, NULL, JBLK_ARRAY,
                   (uint32_t)len);
    if (arr == JREF_NULL)
        return JREF_NULL;
    v->tmp[0] = arr;                    /* root across the second alloc */
    p = (uint16_t *)heap_ptr(&v->heap, arr);
    for (i = 0; i < len; i++)
        p[i] = chars[i];
    obj = vm_alloc(v, (size_t)s->nslots * sizeof(jslot), s, 0, 0);
    v->tmp[0] = JREF_NULL;
    if (obj == JREF_NULL)
        return JREF_NULL;
    f = (jslot *)heap_ptr(&v->heap, obj);
    f[vslot].r = arr;
    f[cslot].i = len;
    return obj;
}

jref vm_make_string(vm *v, const uint8_t *mutf8, int len)
{
    /* Modified UTF-8 -> UTF-16: 1..3 byte sequences, 0 encoded as c0 80. */
    uint16_t *tmp;
    int i = 0, n = 0;
    jref hit, s;

    if (len < 0)
        return JREF_NULL;
    tmp = malloc((size_t)(len ? len : 1) * sizeof(uint16_t));
    if (!tmp)
        return JREF_NULL;
    while (i < len) {
        uint8_t b = mutf8[i];
        if (b < 0x80) {
            tmp[n++] = b;
            i++;
        } else if ((b & 0xe0) == 0xc0 && i + 1 < len) {
            tmp[n++] = (uint16_t)(((b & 0x1f) << 6) |
                                  (mutf8[i + 1] & 0x3f));
            i += 2;
        } else if ((b & 0xf0) == 0xe0 && i + 2 < len) {
            tmp[n++] = (uint16_t)(((b & 0x0f) << 12) |
                                  ((mutf8[i + 1] & 0x3f) << 6) |
                                  (mutf8[i + 2] & 0x3f));
            i += 3;
        } else {
            tmp[n++] = '?';
            i++;
        }
    }
    hit = vm_intern_find(v, tmp, n);
    if (hit != JREF_NULL) {
        free(tmp);
        return hit;
    }
    s = vm_make_string16(v, tmp, n);
    free(tmp);
    if (s != JREF_NULL && v->ninterns < VM_MAX_INTERN)
        v->interns[v->ninterns++] = s;
    return s;
}

jref vm_make_cstring(vm *v, const char *s)
{
    size_t n = strlen(s), i;
    uint16_t *tmp;
    jref r;
    tmp = malloc((n ? n : 1) * sizeof(uint16_t));
    if (!tmp)
        return JREF_NULL;
    for (i = 0; i < n; i++)
        tmp[i] = (uint8_t)s[i];
    r = vm_make_string16(v, tmp, (int)n);
    free(tmp);
    return r;
}

int vm_str_chars(vm *v, jref s, const uint16_t **chars, int *len)
{
    uint32_t vslot, cslot;
    jclass *sc;
    jslot *f;

    *chars = NULL;
    *len = 0;
    if (s == JREF_NULL)
        return FX_ERR_NOTFOUND;
    sc = vm_load_class(v, "java/lang/String");
    if (!sc || string_slots(v, &vslot, &cslot) != FX_OK)
        return FX_ERR_NOTFOUND;
    f = (jslot *)heap_ptr(&v->heap, s);
    *len = f[cslot].i;
    if (f[vslot].r == JREF_NULL) {
        *len = 0;
        return FX_OK;
    }
    *chars = (const uint16_t *)heap_ptr(&v->heap, f[vslot].r);
    return FX_OK;
}

bool vm_str_equals16(vm *v, jref s, const uint16_t *chars, int len)
{
    const uint16_t *c;
    int n, i;
    if (vm_str_chars(v, s, &c, &n) != FX_OK || n != len)
        return false;
    for (i = 0; i < n; i++)
        if (c[i] != chars[i])
            return false;
    return true;
}

jref vm_intern_find(vm *v, const uint16_t *chars, int len)
{
    int i;
    for (i = 0; i < v->ninterns; i++)
        if (vm_str_equals16(v, v->interns[i], chars, len))
            return v->interns[i];
    return JREF_NULL;
}

jref vm_class_mirror(vm *v, jclass *c)
{
    jclass *cc;
    jslot *f;
    uint32_t slot;
    bool is_ref;

    if (c->mirror)
        return c->mirror;
    cc = vm_load_class(v, "java/lang/Class");
    if (!cc)
        return JREF_NULL;
    c->mirror = vm_alloc(v, (size_t)cc->nslots * sizeof(jslot), cc, 0, 0);
    if (c->mirror == JREF_NULL)
        return JREF_NULL;
    if (class_find_field(cc, "name", "Ljava/lang/String;", false, &slot,
                         &is_ref) == FX_OK) {
        v->tmp[0] = c->mirror;
        f = (jslot *)heap_ptr(&v->heap, c->mirror);
        f[slot].r = vm_make_cstring(v, c->name);
        v->tmp[0] = JREF_NULL;
    }
    return c->mirror;
}

/*--- exceptions --------------------------------------------------------*/

jref vm_make_exception(vm *v, jthread *t, const char *cls, const char *msg)
{
    jclass *cl;
    jref obj;
    jslot *f;
    uint32_t slot;
    bool is_ref;

    (void)t;
    cl = vm_load_class(v, cls);
    if (!cl)
        return JREF_NULL;
    obj = vm_alloc(v, (size_t)cl->nslots * sizeof(jslot), cl, 0, 0);
    if (obj == JREF_NULL)
        return JREF_NULL;
    if (msg) {
        v->tmp[1] = obj;                /* root while building the message */
        if (class_find_field(cl, "message", "Ljava/lang/String;", false,
                             &slot, &is_ref) == FX_OK) {
            f = (jslot *)heap_ptr(&v->heap, obj);
            f[slot].r = vm_make_cstring(v, msg);
        }
        v->tmp[1] = JREF_NULL;
    }
    return obj;
}

int vm_throw(vm *v, jthread *t, const char *cls, const char *msg)
{
    t->exception = vm_make_exception(v, t, cls, msg);
    if (t->exception == JREF_NULL) {
        snprintf(v->errbuf, sizeof v->errbuf, "%s: %s", cls,
                 msg ? msg : "");
        t->error = v->errbuf;
    }
    return FX_ERR_JAVA;
}

void vm_exc_text(vm *v, jthread *t, char *buf, size_t cap)
{
    jblk *b;
    jclass *cl = NULL;
    const char *name = "java.lang.Throwable";
    char msg[96];
    size_t i;

    msg[0] = 0;
    if (t->exception != JREF_NULL) {
        b = heap_hdr(&v->heap, t->exception);
        cl = b ? b->cls : NULL;
        if (cl && cl->name)
            name = cl->name;
        if (cl) {
            uint32_t slot;
            bool is_ref;
            if (class_find_field(cl, "message", "Ljava/lang/String;", false,
                                 &slot, &is_ref) == FX_OK) {
                const uint16_t *chars;
                int n, k;
                jslot *f = (jslot *)heap_ptr(&v->heap, t->exception);
                if (vm_str_chars(v, f[slot].r, &chars, &n) == FX_OK) {
                    if (n > 80)
                        n = 80;
                    for (k = 0; k < n; k++)
                        msg[k] = (char)chars[k];
                    msg[n] = 0;
                }
            }
        }
    }
    snprintf(buf, cap, "%s%s%s", name, msg[0] ? ": " : "", msg);
    for (i = 0; buf[i]; i++)
        if (buf[i] == '/')
            buf[i] = '.';
}

/* Is the exception object an instance of class `name` (or a subclass)?
 * Walks the resolved super chain by name; interfaces are not matched
 * (Throwable hierarchies in MIDlets are class-based). */
static bool exc_isa(vm *v, jref exc, const char *name)
{
    jblk *b = heap_hdr(&v->heap, exc);
    jclass *c;
    if (!b || !b->cls)
        return false;
    for (c = b->cls; c; c = c->super)
        if (strcmp(c->name, name) == 0)
            return true;
    /* Unresolved supers: compare the declared chain by name. */
    for (c = b->cls; c; c = c->super) {
        const char *n = c->super_name;
        if (!n)
            break;
        if (strcmp(n, name) == 0)
            return true;
    }
    return false;
}

/* Handler lookup for an exception at code offset `off` of `m`.
 * Returns the handler pc, or -1 if this frame does not catch it. */
static int find_handler(vm *v, jclass *cls, jmethod *m, uint32_t off, jref exc)
{
    int i;
    for (i = 0; i < m->exc_len; i++) {
        const uint8_t *e = m->exc_table + (size_t)i * 8;
        uint16_t start = rd_u2(e);
        uint16_t end = rd_u2(e + 2);
        uint16_t handler = rd_u2(e + 4);
        uint16_t ctype = rd_u2(e + 6);
        const char *cname;

        if (off < start || off >= end)
            continue;
        if (ctype == 0)
            return handler;             /* finally / catch-all */
        if (cp_class_name(cls, ctype, &cname) == FX_OK &&
            exc_isa(v, exc, cname))
            return handler;
    }
    return -1;
}

/*--- misc helpers ------------------------------------------------------*/

static int find_static(jclass *cls, const char *name, const char *desc,
                       jclass **owner, uint32_t *slot, bool *is_ref)
{
    for (; cls; cls = cls->super) {
        int i;
        for (i = 0; i < cls->nstatics; i++)
            if (strcmp(cls->statics[i].name, name) == 0 &&
                strcmp(cls->statics[i].desc, desc) == 0) {
                *owner = cls;
                *slot = cls->statics[i].slot;
                *is_ref = cls->statics[i].is_ref;
                return FX_OK;
            }
    }
    return FX_ERR_NOTFOUND;
}

static int atype_size(int atype)
{
    switch (atype) {
    case 4: case 8:  return 1;   /* boolean, byte  */
    case 5: case 9:  return 2;   /* char, short    */
    case 6: case 10: return 4;   /* float, int     */
    case 7: case 11: return 8;   /* double, long   */
    default: return -1;
    }
}

static int32_t f2i_sat(double d)
{
    if (d != d) return 0;
    if (d >= 2147483647.0) return INT32_MAX;
    if (d <= -2147483648.0) return INT32_MIN;
    return (int32_t)d;
}

static int64_t f2l_sat(double d)
{
    if (d != d) return 0;
    if (d >= 9223372036854775807.0) return INT64_MAX;
    if (d <= -9223372036854775808.0) return INT64_MIN;
    return (int64_t)d;
}

/* Java frem/drem: a - trunc(a/b) * b, without libm (SH target has none). */
static double jrem_d(double a, double b)
{
    double q;
    if (b == 0.0 || a != a || b != b || a - a != 0.0 || b - b != 0.0)
        return __builtin_nan("");
    q = a / b;
    if (q >= 9223372036854775808.0 || q <= -9223372036854775808.0)
        return a;               /* |a/b| beyond int64: remainder is a */
    q = (double)(int64_t)q;     /* truncate toward zero */
    return a - q * b;
}

/*--- array allocation --------------------------------------------------*/

static int elem_size_of(const char *desc)
{
    /* desc points past '[' to the element type char */
    switch (desc[0]) {
    case 'B': case 'Z': return 1;
    case 'C': case 'S': return 2;
    case 'I': case 'F': return 4;
    case 'J': case 'D': return 8;
    case 'L': case '[': return 4;   /* reference */
    default: return -1;
    }
}

static jref alloc_multi(vm *v, const char *desc, const int32_t *counts,
                        int depth)
{
    int32_t n = counts[depth];
    jref arr;
    uint8_t *base;
    int esize;

    if (n < 0)
        return JREF_NULL;
    esize = elem_size_of(desc + 1);
    if (esize < 0)
        return JREF_NULL;

    if (desc[1] == '[') {
        int32_t i;
        arr = vm_alloc(v, (size_t)n * 4, NULL,
                       JBLK_ARRAY | JBLK_REFELEM, (uint32_t)n);
        if (arr == JREF_NULL)
            return JREF_NULL;
        base = heap_ptr(&v->heap, arr);
        for (i = 0; i < n; i++) {
            jref e = alloc_multi(v, desc + 1, counts, depth + 1);
            if (e == JREF_NULL && n > 0 && counts[depth + 1] != 0)
                return JREF_NULL;
            ((jref *)base)[i] = e;
        }
        return arr;
    }

    arr = vm_alloc(v, (size_t)n * (size_t)esize, NULL,
                   (uint32_t)(JBLK_ARRAY |
                              (desc[1] == 'L' ? JBLK_REFELEM : 0)),
                   (uint32_t)n);
    return arr;
}

/*--- interpreter -------------------------------------------------------*/

static int run(vm *v, jthread *t, int base_depth, jslot *ret_out,
               int *ret_n);

int vm_call(vm *v, jthread *t, jclass *cls, const char *method,
            const char *desc, jref receiver, const jslot *args, jslot *ret)
{
    jclass *decl = cls;
    jmethod *m = class_find_method_decl(cls, method, desc, &decl);
    jframe *nf;
    uint32_t ai, li;
    int ret_n = 0, base;
    const char *p;

    if (!m || !m->code) {
        snprintf(v->errbuf, sizeof v->errbuf, "missing method: %.48s.%.48s%.48s",
                 cls->name ? cls->name : "?", method, desc);
        t->error = v->errbuf;
        return FX_ERR_NOTFOUND;
    }
    if (t->nframes >= VM_MAX_FRAMES ||
        t->ltop + m->max_locals > VM_LOCALS_SLOTS) {
        t->error = "StackOverflowError";
        return FX_ERR_JAVA;
    }

    nf = &t->frames[t->nframes];
    nf->m = m;
    nf->cls = decl;
    nf->pc = m->code;
    nf->locals = &t->locals[t->ltop];
    nf->sp_base = 0;
    memset(nf->locals, 0, sizeof(jslot) * (size_t)m->max_locals);
    memset(&t->ltypes[t->ltop], 0, m->max_locals);

    ai = 0;
    li = 0;
    if (receiver != JREF_NULL) {
        nf->locals[li].r = receiver;
        t->ltypes[t->ltop + li] = 1;
        li = 1;
    }
    /* Type each argument slot from the descriptor (refs matter for GC). */
    p = desc + 1;
    while (*p && *p != ')' && li < m->max_locals) {
        bool is_ref = false, wide = false;
        while (*p == '[') {
            is_ref = true;
            p++;
        }
        if (*p == 'L') {
            is_ref = true;
            while (*p && *p != ';')
                p++;
            if (*p == ';')
                p++;
        } else {
            wide = (*p == 'J' || *p == 'D');
            p++;
        }
        nf->locals[li] = args[ai];
        t->ltypes[t->ltop + li] = is_ref ? 1 : 0;
        ai++;
        li++;
        if (wide && li < m->max_locals) {
            nf->locals[li] = args[ai];
            t->ltypes[t->ltop + li] = 2;
            ai++;
            li++;
        }
    }

    t->ltop += m->max_locals;
    base = t->nframes;
    t->nframes++;

    return run(v, t, base, ret, &ret_n);
}

int vm_call_static(vm *v, jthread *t, const char *cls, const char *method,
                   const char *desc, const jslot *args, jslot *ret)
{
    jclass *c = vm_load_class(v, cls);
    if (!c) {
        t->error = v->errbuf[0] ? v->errbuf : "class not found";
        return FX_ERR_NOTFOUND;
    }
    return vm_call(v, t, c, method, desc, JREF_NULL, args, ret);
}

/*--- the dispatch loop -------------------------------------------------*/

static int run(vm *v, jthread *t, int base_depth, jslot *ret_out,
               int *ret_n)
{
    jheap *hp = &v->heap;
    jframe *fr = &t->frames[t->nframes - 1];
    jmethod *m = fr->m;
    jclass *cls = fr->cls;
    const uint8_t *code = m->code;
    const uint8_t *pc = fr->pc;
    const uint8_t *ipc = pc;      /* start of the current instruction */
    jslot *stack = t->stack;
    uint8_t *ttypes = t->ttypes;
    jslot *locals = fr->locals;
    uint8_t *ltypes = &t->ltypes[fr->locals - t->locals];
    int sp = fr->sp_base;

#define FAULT(msg) do { t->error = (msg); return FX_ERR_JAVA; } while (0)
/* Raise a catchable Java exception; falls back to FAULT when the class
 * library is absent or the heap cannot hold the exception object. */
#define THROW(clsname, msgstr) do { \
        t->exception = vm_make_exception(v, t, (clsname), (msgstr)); \
        if (t->exception == JREF_NULL) FAULT(msgstr); \
        goto do_throw; \
    } while (0)
#define RELOAD() do { \
        fr = &t->frames[t->nframes - 1]; m = fr->m; cls = fr->cls; \
        code = m->code; pc = fr->pc; locals = fr->locals; \
        ltypes = &t->ltypes[fr->locals - t->locals]; sp = fr->sp_base; \
    } while (0)
#define U1() (*pc++)
#define U2() (pc += 2, rd_u2(pc - 2))
#define S1() ((int8_t)*pc++)
#define S2() (pc += 2, (int16_t)rd_u2(pc - 2))
#define S4() (pc += 4, rd_s4(pc - 4))
#define PUSH(x) do { \
        if (sp >= VM_STACK_SLOTS) FAULT("StackOverflowError"); \
        stack[sp].i = (x); ttypes[sp] = 0; sp++; \
    } while (0)
#define PUSH_F(x) do { \
        if (sp >= VM_STACK_SLOTS) FAULT("StackOverflowError"); \
        stack[sp].f = (x); ttypes[sp] = 0; sp++; \
    } while (0)
#define PUSH_R(x) do { \
        if (sp >= VM_STACK_SLOTS) FAULT("StackOverflowError"); \
        stack[sp].r = (x); ttypes[sp] = 1; sp++; \
    } while (0)
#define PUSH_W(lo, hi) do { \
        if (sp + 1 >= VM_STACK_SLOTS) FAULT("StackOverflowError"); \
        stack[sp].i = (lo); ttypes[sp] = 0; sp++; \
        stack[sp].i = (hi); ttypes[sp] = 2; sp++; \
    } while (0)
#define PUSH_I64(x) do { jslot _w[2]; jslot_set_long(_w, (x)); \
                         PUSH_W(_w[0].i, _w[1].i); } while (0)
#define PUSH_D(x) do { jslot _w[2]; jslot_set_double(_w, (x)); \
                       PUSH_W(_w[0].i, _w[1].i); } while (0)
#define CHK_LOCAL(ix) do { \
        if ((uint32_t)(ix) >= m->max_locals) FAULT("bad local index"); \
    } while (0)
#define ENSURE_INIT(c) do { \
        jclass *_c = (c); \
        if (!_c->initialized) { \
            jmethod *_ci; \
            jclass *_cd = _c; \
            _c->initialized = true; \
            _ci = class_find_method_decl(_c, "<clinit>", "()V", &_cd); \
            if (_ci && _ci->code) { \
                jframe *_nf; \
                if (t->nframes >= VM_MAX_FRAMES || \
                    t->ltop + _ci->max_locals > VM_LOCALS_SLOTS) \
                    THROW("java/lang/StackOverflowError", "class init"); \
                _nf = &t->frames[t->nframes]; \
                /* Re-execute this instruction after <clinit>: the resume \
                 * point is the instruction start, not past its operands. */ \
                fr->pc = ipc; \
                _nf->m = _ci; _nf->cls = _cd; _nf->pc = _ci->code; \
                _nf->caller_ipc = ipc; \
                _nf->locals = &t->locals[t->ltop]; _nf->sp_base = sp; \
                memset(_nf->locals, 0, sizeof(jslot) * (size_t)_ci->max_locals); \
                memset(&t->ltypes[t->ltop], 0, _ci->max_locals); \
                t->ltop += _ci->max_locals; t->nframes++; \
                RELOAD(); \
                goto dispatch; \
            } \
        } \
    } while (0)

    for (;;) {
        uint8_t op;

dispatch:
        ipc = pc;
        op = U1();

        switch (op) {

        /* --- constants ------------------------------------------------ */
        case 0x00: break;                                /* nop        */
        case 0x01: PUSH_R(JREF_NULL); break;             /* aconst_null */
        case 0x02: case 0x03: case 0x04: case 0x05:
        case 0x06: case 0x07: case 0x08:                 /* iconst_m1..5 */
            PUSH((int32_t)op - 0x03);
            break;
        case 0x09: PUSH_I64(0); break;                   /* lconst_0/1 */
        case 0x0a: PUSH_I64(1); break;
        case 0x0b: PUSH_F(0.0f); break;                  /* fconst_0/1/2 */
        case 0x0c: PUSH_F(1.0f); break;
        case 0x0d: PUSH_F(2.0f); break;
        case 0x0e: PUSH_D(0.0); break;                   /* dconst_0/1 */
        case 0x0f: PUSH_D(1.0); break;
        case 0x10: PUSH((int32_t)S1()); break;           /* bipush */
        case 0x11: PUSH((int32_t)S2()); break;           /* sipush */
        case 0x12: case 0x13: {                          /* ldc, ldc_w */
            uint16_t idx = (op == 0x12) ? U1() : U2();
            if (idx <= 0 || idx >= cls->cp_count)
                FAULT("bad ldc index");
            switch (cls->cp[idx].tag) {
            case CP_INTEGER: PUSH(cp_ival(&cls->cp[idx])); break;
            case CP_FLOAT:   PUSH_F(cp_fval(&cls->cp[idx])); break;
            case CP_STRING: {
                const uint8_t *sv; uint16_t sl;
                jref s;
                if (cp_string_utf8(cls, idx, &sv, &sl) != FX_OK)
                    FAULT("ldc: bad string constant");
                s = vm_make_string(v, sv, sl);
                if (s == JREF_NULL) THROW("java/lang/OutOfMemoryError", "ldc");
                PUSH_R(s);
                break;
            }
            default: FAULT("ldc: unsupported constant type");
            }
            break;
        }
        case 0x14: {                                     /* ldc2_w */
            uint16_t idx = U2();
            if (idx <= 0 || idx >= cls->cp_count)
                FAULT("bad ldc2_w index");
            if (cls->cp[idx].tag == CP_LONG)
                PUSH_I64(cp_lval(cls, idx));
            else if (cls->cp[idx].tag == CP_DOUBLE)
                PUSH_D(cp_dval(cls, idx));
            else
                FAULT("ldc2_w: bad constant");
            break;
        }

        /* --- loads ---------------------------------------------------- */
        case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: {
            uint32_t ix = U1();
            CHK_LOCAL(ix);
            switch (op) {
            case 0x15: PUSH(locals[ix].i); break;                    /* iload */
            case 0x16:                                             /* lload */
                CHK_LOCAL(ix + 1);
                PUSH_W(locals[ix].i, locals[ix + 1].i);
                break;
            case 0x17: PUSH_F(locals[ix].f); break;                  /* fload */
            case 0x18:                                             /* dload */
                CHK_LOCAL(ix + 1);
                PUSH_W(locals[ix].i, locals[ix + 1].i);
                break;
            case 0x19: PUSH_R(locals[ix].r); break;                  /* aload */
            }
            break;
        }
        case 0x1a: case 0x1b: case 0x1c: case 0x1d:     /* iload_0..3 */
            PUSH(locals[op - 0x1a].i);
            break;
        case 0x1e: case 0x1f: case 0x20: case 0x21:     /* lload_0..3 */
            PUSH_W(locals[op - 0x1e].i, locals[op - 0x1e + 1].i);
            break;
        case 0x22: case 0x23: case 0x24: case 0x25:     /* fload_0..3 */
            PUSH_F(locals[op - 0x22].f);
            break;
        case 0x26: case 0x27: case 0x28: case 0x29:     /* dload_0..3 */
            PUSH_W(locals[op - 0x26].i, locals[op - 0x26 + 1].i);
            break;
        case 0x2a: case 0x2b: case 0x2c: case 0x2d:     /* aload_0..3 */
            PUSH_R(locals[op - 0x2a].r);
            break;

        /* --- array loads ---------------------------------------------- */
        case 0x2e: case 0x2f: case 0x30: case 0x31: case 0x32:
        case 0x33: case 0x34: case 0x35: {
            int32_t ix;
            jref arr;
            jblk *b;
            uint8_t *base;
            ix = stack[--sp].i;
            arr = stack[--sp].r;
            if (arr == JREF_NULL) THROW("java/lang/NullPointerException", "null");
            b = heap_hdr(hp, arr);
            if (ix < 0 || (uint32_t)ix >= b->aux)
                THROW("java/lang/ArrayIndexOutOfBoundsException", "index");
            base = (uint8_t *)heap_ptr(hp, arr);
            switch (op) {
            case 0x2e: PUSH(((int32_t *)base)[ix]); break;           /* iaload */
            case 0x2f:                                               /* laload */
                PUSH_W(((int32_t *)base)[ix * 2], ((int32_t *)base)[ix * 2 + 1]);
                break;
            case 0x30: PUSH_F(((float *)base)[ix]); break;           /* faload */
            case 0x31:                                               /* daload */
                PUSH_D(((double *)base)[ix]);
                break;
            case 0x32: PUSH_R(((jref *)base)[ix]); break;            /* aaload */
            case 0x33: PUSH((int32_t)((int8_t *)base)[ix]); break;   /* baload */
            case 0x34: PUSH((int32_t)((uint16_t *)base)[ix]); break; /* caload */
            case 0x35: PUSH((int32_t)((int16_t *)base)[ix]); break;  /* saload */
            }
            break;
        }

        /* --- stores --------------------------------------------------- */
        case 0x36: case 0x37: case 0x38: case 0x39: case 0x3a: {
            uint32_t ix = U1();
            CHK_LOCAL(ix);
            switch (op) {
            case 0x36: locals[ix].i = stack[--sp].i; ltypes[ix] = 0; break;
            case 0x37:                                             /* lstore */
                CHK_LOCAL(ix + 1);
                locals[ix + 1].i = stack[--sp].i;
                locals[ix].i = stack[--sp].i;
                ltypes[ix] = 0; ltypes[ix + 1] = 2;
                break;
            case 0x38: locals[ix].f = stack[--sp].f; ltypes[ix] = 0; break;
            case 0x39:                                             /* dstore */
                CHK_LOCAL(ix + 1);
                locals[ix + 1].i = stack[--sp].i;
                locals[ix].i = stack[--sp].i;
                ltypes[ix] = 0; ltypes[ix + 1] = 2;
                break;
            case 0x3a: locals[ix].r = stack[--sp].r; ltypes[ix] = 1; break;
            }
            break;
        }
        case 0x3b: case 0x3c: case 0x3d: case 0x3e:     /* istore_0..3 */
            locals[op - 0x3b].i = stack[--sp].i;
            ltypes[op - 0x3b] = 0;
            break;
        case 0x3f: case 0x40: case 0x41: case 0x42: {   /* lstore_0..3 */
            int ix = op - 0x3f;
            locals[ix + 1].i = stack[--sp].i;
            locals[ix].i = stack[--sp].i;
            ltypes[ix] = 0; ltypes[ix + 1] = 2;
            break;
        }
        case 0x43: case 0x44: case 0x45: case 0x46:     /* fstore_0..3 */
            locals[op - 0x43].f = stack[--sp].f;
            ltypes[op - 0x43] = 0;
            break;
        case 0x47: case 0x48: case 0x49: case 0x4a: {   /* dstore_0..3 */
            int ix = op - 0x47;
            locals[ix + 1].i = stack[--sp].i;
            locals[ix].i = stack[--sp].i;
            ltypes[ix] = 0; ltypes[ix + 1] = 2;
            break;
        }
        case 0x4b: case 0x4c: case 0x4d: case 0x4e:     /* astore_0..3 */
            locals[op - 0x4b].r = stack[--sp].r;
            ltypes[op - 0x4b] = 1;
            break;

        /* --- array stores --------------------------------------------- */
        case 0x4f: case 0x50: case 0x51: case 0x52: case 0x53:
        case 0x54: case 0x55: case 0x56: {
            jref arr;
            jblk *b;
            uint8_t *base;
            int32_t ix;
            if (op == 0x50 || op == 0x52) {              /* lastore, dastore */
                int64_t val64;
                double vald;
                if (op == 0x50) {
                    val64 = jslot_get_long(&stack[sp - 2]);
                    sp -= 2;
                } else {
                    vald = jslot_get_double(&stack[sp - 2]);
                    sp -= 2;
                }
                ix = stack[--sp].i;
                arr = stack[--sp].r;
                if (arr == JREF_NULL) THROW("java/lang/NullPointerException", "null");
                b = heap_hdr(hp, arr);
                if (ix < 0 || (uint32_t)ix >= b->aux)
                    THROW("java/lang/ArrayIndexOutOfBoundsException", "index");
                base = (uint8_t *)heap_ptr(hp, arr);
                if (op == 0x50)
                    jslot_set_long((jslot *)(void *)(base + (size_t)ix * 8), val64);
                else
                    jslot_set_double((jslot *)(void *)(base + (size_t)ix * 8), vald);
                break;
            }
            {
                jslot val = stack[--sp];
                ix = stack[--sp].i;
                arr = stack[--sp].r;
                if (arr == JREF_NULL) THROW("java/lang/NullPointerException", "null");
                b = heap_hdr(hp, arr);
                if (ix < 0 || (uint32_t)ix >= b->aux)
                    THROW("java/lang/ArrayIndexOutOfBoundsException", "index");
                base = (uint8_t *)heap_ptr(hp, arr);
                switch (op) {
                case 0x4f: ((int32_t *)base)[ix] = val.i; break;    /* iastore */
                case 0x51: ((float *)base)[ix] = val.f; break;      /* fastore */
                case 0x53: ((jref *)base)[ix] = val.r; break;       /* aastore */
                case 0x54: ((int8_t *)base)[ix] = (int8_t)val.i; break;  /* bastore */
                case 0x55: ((uint16_t *)base)[ix] = (uint16_t)val.i; break; /* castore */
                case 0x56: ((int16_t *)base)[ix] = (int16_t)val.i; break;  /* sastore */
                }
            }
            break;
        }

        /* --- stack shuffles ------------------------------------------- */
        case 0x57:                                         /* pop */
            if (sp <= 0) FAULT("verify: stack underflow");
            sp--;
            break;
        case 0x58:                                         /* pop2 */
            if (sp < 2) FAULT("verify: stack underflow");
            sp -= 2;
            break;
        case 0x59:                                         /* dup */
            if (sp <= 0) FAULT("verify: stack underflow");
            stack[sp] = stack[sp - 1];
            ttypes[sp] = ttypes[sp - 1];
            sp++;
            break;
        case 0x5a:                                         /* dup_x1 */
            if (sp < 2) FAULT("verify: stack underflow");
            {
                jslot v1 = stack[sp - 1];
                uint8_t t1 = ttypes[sp - 1];
                stack[sp - 1] = stack[sp - 2];
                ttypes[sp - 1] = ttypes[sp - 2];
                stack[sp - 2] = v1;
                ttypes[sp - 2] = t1;
                stack[sp] = v1;
                ttypes[sp] = t1;
                sp++;
            }
            break;
        case 0x5b:                                         /* dup_x2 */
            if (sp < 3) FAULT("verify: stack underflow");
            {
                jslot v1 = stack[sp - 1];
                uint8_t t1 = ttypes[sp - 1];
                stack[sp - 1] = stack[sp - 2];
                ttypes[sp - 1] = ttypes[sp - 2];
                stack[sp - 2] = stack[sp - 3];
                ttypes[sp - 2] = ttypes[sp - 3];
                stack[sp - 3] = v1;
                ttypes[sp - 3] = t1;
                stack[sp] = v1;
                ttypes[sp] = t1;
                sp++;
            }
            break;
        case 0x5c:                                         /* dup2 */
            if (sp < 2) FAULT("verify: stack underflow");
            stack[sp] = stack[sp - 2]; ttypes[sp] = ttypes[sp - 2];
            stack[sp + 1] = stack[sp - 1]; ttypes[sp + 1] = ttypes[sp - 1];
            sp += 2;
            break;
        case 0x5d:                                         /* dup2_x1 */
            if (sp < 3) FAULT("verify: stack underflow");
            {
                jslot v[3];
                uint8_t vt[3];
                int i;
                for (i = 0; i < 3; i++) {
                    v[i] = stack[sp - 3 + i];
                    vt[i] = ttypes[sp - 3 + i];
                }
                /* [v0, v1, v2] -> [v1, v2, v0, v1, v2] */
                stack[sp - 3] = v[1]; ttypes[sp - 3] = vt[1];
                stack[sp - 2] = v[2]; ttypes[sp - 2] = vt[2];
                stack[sp - 1] = v[0]; ttypes[sp - 1] = vt[0];
                stack[sp]     = v[1]; ttypes[sp]     = vt[1];
                stack[sp + 1] = v[2]; ttypes[sp + 1] = vt[2];
                sp += 2;
            }
            break;
        case 0x5e:                                         /* dup2_x2 */
            if (sp < 4) FAULT("verify: stack underflow");
            {
                jslot v[4];
                uint8_t vt[4];
                int i;
                for (i = 0; i < 4; i++) {
                    v[i] = stack[sp - 4 + i];
                    vt[i] = ttypes[sp - 4 + i];
                }
                /* [v0, v1, v2, v3] -> [v2, v3, v0, v1, v2, v3] */
                stack[sp - 4] = v[2]; ttypes[sp - 4] = vt[2];
                stack[sp - 3] = v[3]; ttypes[sp - 3] = vt[3];
                stack[sp - 2] = v[0]; ttypes[sp - 2] = vt[0];
                stack[sp - 1] = v[1]; ttypes[sp - 1] = vt[1];
                stack[sp]     = v[2]; ttypes[sp]     = vt[2];
                stack[sp + 1] = v[3]; ttypes[sp + 1] = vt[3];
                sp += 2;
            }
            break;
        case 0x5f:                                         /* swap */
            if (sp < 2) FAULT("verify: stack underflow");
            {
                jslot tmp = stack[sp - 1];
                uint8_t tmpt = ttypes[sp - 1];
                stack[sp - 1] = stack[sp - 2];
                ttypes[sp - 1] = ttypes[sp - 2];
                stack[sp - 2] = tmp;
                ttypes[sp - 2] = tmpt;
            }
            break;

        /* --- arithmetic ----------------------------------------------- */
        /* int: 0x60 add 0x64 sub 0x68 mul 0x6c div 0x70 rem 0x74 neg
         *      0x78 shl 0x7a shr 0x7c ushr 0x7e and 0x80 or  0x82 xor */
        case 0x60: case 0x64: case 0x68: case 0x6c: case 0x70:
        case 0x78: case 0x7a: case 0x7c: case 0x7e: case 0x80: case 0x82: {
            int32_t b, a;
            b = stack[--sp].i;
            a = stack[--sp].i;
            switch (op) {
            case 0x60: PUSH(a + b); break;
            case 0x64: PUSH(a - b); break;
            case 0x68: PUSH(a * b); break;
            case 0x6c:                                 /* idiv */
                if (b == 0) THROW("java/lang/ArithmeticException", "/ by zero");
                if (a == INT32_MIN && b == -1) PUSH(INT32_MIN);
                else PUSH(a / b);
                break;
            case 0x70:                                 /* irem */
                if (b == 0) THROW("java/lang/ArithmeticException", "/ by zero");
                if (a == INT32_MIN && b == -1) PUSH(0);
                else PUSH(a % b);
                break;
            case 0x78: PUSH(a << (b & 31)); break;     /* ishl  */
            case 0x7a: PUSH(a >> (b & 31)); break;     /* ishr  */
            case 0x7c:                                 /* iushr */
                PUSH((int32_t)((uint32_t)a >> (b & 31)));
                break;
            case 0x7e: PUSH(a & b); break;             /* iand  */
            case 0x80: PUSH(a | b); break;             /* ior   */
            case 0x82: PUSH(a ^ b); break;             /* ixor  */
            }
            break;
        }
        case 0x74: { int32_t a = stack[--sp].i; PUSH(-a); break; } /* ineg */
        case 0x84: {                                       /* iinc */
            uint32_t ix = U1();
            int32_t cn = (int32_t)S1();
            CHK_LOCAL(ix);
            locals[ix].i += cn;
            ltypes[ix] = 0;
            break;
        }

        /* long: 0x61 add 0x65 sub 0x69 mul 0x6d div 0x71 rem 0x75 neg
         *      0x79 shl 0x7b shr 0x7d ushr 0x7f and 0x81 or  0x83 xor */
        case 0x61: case 0x65: case 0x69: case 0x6d: case 0x71:
        case 0x79: case 0x7b: case 0x7d: case 0x7f: case 0x81: case 0x83: {
            int64_t b, a;
            int32_t s;
            if (op == 0x79 || op == 0x7b || op == 0x7d) {
                s = stack[--sp].i;
                sp -= 2;
                a = jslot_get_long(&stack[sp]);
                switch (op) {
                case 0x79: PUSH_I64(a << (s & 63)); break;
                case 0x7b: PUSH_I64(a >> (s & 63)); break;
                case 0x7d: PUSH_I64((int64_t)((uint64_t)a >> (s & 63))); break;
                }
                break;
            }
            sp -= 2;
            b = jslot_get_long(&stack[sp]);
            sp -= 2;
            a = jslot_get_long(&stack[sp]);
            switch (op) {
            case 0x61: PUSH_I64(a + b); break;
            case 0x65: PUSH_I64(a - b); break;
            case 0x69: PUSH_I64(a * b); break;
            case 0x6d:
                if (b == 0) THROW("java/lang/ArithmeticException", "/ by zero");
                if (a == INT64_MIN && b == -1) PUSH_I64(INT64_MIN);
                else PUSH_I64(a / b);
                break;
            case 0x71:
                if (b == 0) THROW("java/lang/ArithmeticException", "/ by zero");
                if (a == INT64_MIN && b == -1) PUSH_I64(0);
                else PUSH_I64(a % b);
                break;
            case 0x7f: PUSH_I64(a & b); break;         /* land */
            case 0x81: PUSH_I64(a | b); break;         /* lor  */
            case 0x83: PUSH_I64(a ^ b); break;         /* lxor */
            }
            break;
        }
        case 0x75: { sp -= 2; int64_t a = jslot_get_long(&stack[sp]);
                     PUSH_I64(-a); break; }             /* lneg */

        /* float/double arithmetic */
        case 0x62: case 0x66: case 0x6a: case 0x6e: case 0x72: {
            float b, a;
            b = stack[--sp].f;
            a = stack[--sp].f;
            switch (op) {
            case 0x62: PUSH_F(a + b); break;
            case 0x66: PUSH_F(a - b); break;
            case 0x6a: PUSH_F(a * b); break;
            case 0x6e: PUSH_F(a / b); break;
            case 0x72: PUSH_F(jrem_d((double)a, (double)b)); break;
            }
            break;
        }
        case 0x76: { float a = stack[--sp].f; PUSH_F(-a); break; }
        case 0x63: case 0x67: case 0x6b: case 0x6f: case 0x73: {
            double b, a;
            sp -= 2;
            b = jslot_get_double(&stack[sp]);
            sp -= 2;
            a = jslot_get_double(&stack[sp]);
            switch (op) {
            case 0x63: PUSH_D(a + b); break;
            case 0x67: PUSH_D(a - b); break;
            case 0x6b: PUSH_D(a * b); break;
            case 0x6f: PUSH_D(a / b); break;
            case 0x73: PUSH_D(jrem_d(a, b)); break;
            }
            break;
        }
        case 0x77: { sp -= 2; double a = jslot_get_double(&stack[sp]);
                     PUSH_D(-a); break; }               /* dneg */

        /* --- conversions ---------------------------------------------- */
        case 0x85: { int32_t a = stack[--sp].i; PUSH_I64((int64_t)a); break; }
        case 0x86: { int32_t a = stack[--sp].i; PUSH_F((float)a); break; }
        case 0x87: { int32_t a = stack[--sp].i; PUSH_D((double)a); break; }
        case 0x88: { int64_t a = jslot_get_long(&stack[sp - 2]); sp -= 2;
                     PUSH((int32_t)a); break; }
        case 0x89: { int64_t a = jslot_get_long(&stack[sp - 2]); sp -= 2;
                     PUSH_F((float)a); break; }
        case 0x8a: { int64_t a = jslot_get_long(&stack[sp - 2]); sp -= 2;
                     PUSH_D((double)a); break; }
        case 0x8b: { float a = stack[--sp].f; PUSH(f2i_sat((double)a)); break; }
        case 0x8c: { float a = stack[--sp].f; PUSH_I64(f2l_sat((double)a)); break; }
        case 0x8d: { float a = stack[--sp].f; PUSH_D((double)a); break; }
        case 0x8e: { double a = jslot_get_double(&stack[sp - 2]); sp -= 2;
                     PUSH(f2i_sat(a)); break; }
        case 0x8f: { double a = jslot_get_double(&stack[sp - 2]); sp -= 2;
                     PUSH_I64(f2l_sat(a)); break; }
        case 0x90: { double a = jslot_get_double(&stack[sp - 2]); sp -= 2;
                     PUSH_F((float)a); break; }
        case 0x91: { int32_t a = stack[--sp].i; PUSH((int32_t)(int8_t)a); break; }
        case 0x92: { int32_t a = stack[--sp].i; PUSH((int32_t)(uint16_t)a); break; }
        case 0x93: { int32_t a = stack[--sp].i; PUSH((int32_t)(int16_t)a); break; }

        /* --- comparisons ---------------------------------------------- */
        case 0x94: {                                       /* lcmp */
            int64_t b = jslot_get_long(&stack[sp - 2]);
            int64_t a;
            sp -= 2;
            a = jslot_get_long(&stack[sp - 2]);
            sp -= 2;
            PUSH(a > b ? 1 : (a == b ? 0 : -1));
            break;
        }
        case 0x95: case 0x96: {                            /* fcmpl, fcmpg */
            float b, a;
            b = stack[--sp].f;
            a = stack[--sp].f;
            if (a != a || b != b) PUSH(op == 0x95 ? -1 : 1);
            else PUSH(a > b ? 1 : (a == b ? 0 : -1));
            break;
        }
        case 0x97: case 0x98: {                            /* dcmpl, dcmpg */
            double b = jslot_get_double(&stack[sp - 2]);
            double a;
            sp -= 2;
            a = jslot_get_double(&stack[sp - 2]);
            sp -= 2;
            if (a != a || b != b) PUSH(op == 0x97 ? -1 : 1);
            else PUSH(a > b ? 1 : (a == b ? 0 : -1));
            break;
        }

        /* --- branches ------------------------------------------------- */
        case 0x99: case 0x9a: case 0x9b: case 0x9c: case 0x9d: case 0x9e: {
            int32_t a, off, take;
            a = stack[--sp].i;
            off = S2();
            take = 0;
            switch (op) {
            case 0x99: take = (a == 0); break;
            case 0x9a: take = (a != 0); break;
            case 0x9b: take = (a < 0); break;
            case 0x9c: take = (a >= 0); break;
            case 0x9d: take = (a > 0); break;
            case 0x9e: take = (a <= 0); break;
            }
            if (take) pc += off - 3;
            break;
        }
        case 0x9f: case 0xa0: case 0xa1: case 0xa2: case 0xa3: case 0xa4: {
            int32_t b, a, off, take;
            b = stack[--sp].i;
            a = stack[--sp].i;
            off = S2();
            take = 0;
            switch (op) {
            case 0x9f: take = (a == b); break;
            case 0xa0: take = (a != b); break;
            case 0xa1: take = (a < b); break;
            case 0xa2: take = (a >= b); break;
            case 0xa3: take = (a > b); break;
            case 0xa4: take = (a <= b); break;
            }
            if (take) pc += off - 3;
            break;
        }
        case 0xa5: case 0xa6: {                            /* if_acmp* */
            jref b, a;
            int32_t off;
            b = stack[--sp].r;
            a = stack[--sp].r;
            off = S2();
            if ((op == 0xa5) == (a == b))
                pc += off - 3;
            break;
        }
        case 0xc6: case 0xc7: {                            /* ifnull* */
            jref a = stack[--sp].r;
            int32_t off = S2();
            if ((op == 0xc6) == (a == JREF_NULL))
                pc += off - 3;
            break;
        }
        case 0xa7: {                                       /* goto */
            int32_t off = S2();
            pc += off - 3;
            break;
        }
        case 0xc8: {                                       /* goto_w */
            int32_t off = S4();
            pc += off - 5;
            break;
        }
        case 0xa8: {                                       /* jsr */
            int32_t off = S2();
            PUSH((int32_t)(pc - code));
            pc += off - 3;
            break;
        }
        case 0xc9: {                                       /* jsr_w */
            int32_t off = S4();
            PUSH((int32_t)(pc - code));
            pc += off - 5;
            break;
        }
        case 0xa9: {                                       /* ret */
            uint32_t ix = U1();
            CHK_LOCAL(ix);
            pc = code + locals[ix].i;
            break;
        }
        case 0xaa: {                                       /* tableswitch */
            const uint8_t *ins = pc - 1;
            int32_t def, low, high, key;
            pc += (4 - (((int)(ins - code) + 1) & 3)) & 3;
            def = S4(); low = S4(); high = S4();
            key = stack[--sp].i;
            if (key < low || key > high)
                pc = ins + def;
            else {
                pc += (size_t)(key - low) * 4;
                pc = ins + S4();
            }
            break;
        }
        case 0xab: {                                       /* lookupswitch */
            const uint8_t *ins = pc - 1;
            int32_t def, npairs, key, i;
            pc += (4 - (((int)(ins - code) + 1) & 3)) & 3;
            def = S4(); npairs = S4();
            key = stack[--sp].i;
            for (i = 0; i < npairs; i++) {
                int32_t match = S4();
                int32_t off = S4();
                if (match == key) {
                    pc = ins + off;
                    break;
                }
            }
            if (i == npairs)
                pc = ins + def;
            break;
        }

        /* --- returns -------------------------------------------------- */
        case 0xac: case 0xad: case 0xae: case 0xaf: case 0xb0: case 0xb1: {
            int nret = (op == 0xad || op == 0xaf) ? 2 : (op == 0xb1 ? 0 : 1);
            int is_ref = (op == 0xb0);
            jslot rv[2];
            int res_base;
            if (nret > 0)
                rv[0] = stack[sp - nret];
            if (nret == 2)
                rv[1] = stack[sp - 1];
            /* results replace the popped args at the callee's stack base */
            res_base = fr->sp_base;
            t->ltop -= m->max_locals;
            t->nframes--;
            if (t->nframes == base_depth) {
                int i;
                for (i = 0; i < nret; i++)
                    ret_out[i] = rv[i];
                *ret_n = nret;
                return FX_OK;
            }
            RELOAD();
            sp = res_base;
            if (nret > 0) {
                stack[sp] = rv[0];
                ttypes[sp] = is_ref ? 1 : 0;
                sp++;
            }
            if (nret == 2) {
                stack[sp] = rv[1];
                ttypes[sp] = 2;
                sp++;
            }
            break;
        }

        /* --- fields --------------------------------------------------- */
        case 0xb2: case 0xb3: {                            /* get/putstatic */
            uint16_t idx = U2();
            const char *cn, *fn, *fd;
            jclass *fc, *own;
            uint32_t slot;
            bool is_ref;
            if (cp_ref(cls, idx, &cn, &fn, &fd) != FX_OK)
                FAULT("bad field reference");
            fc = vm_load_class(v, cn);
            if (!fc)
                FAULT(v->errbuf[0] ? v->errbuf : "class not found (static field)");
            ENSURE_INIT(fc);
            if (find_static(fc, fn, fd, &own, &slot, &is_ref) != FX_OK)
                FAULT("static field not found");
            if (op == 0xb2) {
                if (fd[0] == 'J' || fd[0] == 'D')
                    PUSH_W(own->static_vals[slot].i, own->static_vals[slot + 1].i);
                else if (is_ref)
                    PUSH_R(own->static_vals[slot].r);
                else if (fd[0] == 'F')
                    PUSH_F(own->static_vals[slot].f);
                else
                    PUSH(own->static_vals[slot].i);
            } else {
                if (fd[0] == 'J' || fd[0] == 'D') {
                    own->static_vals[slot + 1].i = stack[--sp].i;
                    own->static_vals[slot].i = stack[--sp].i;
                } else if (is_ref) {
                    own->static_vals[slot].r = stack[--sp].r;
                } else if (fd[0] == 'F') {
                    own->static_vals[slot].f = stack[--sp].f;
                } else {
                    own->static_vals[slot].i = stack[--sp].i;
                }
            }
            break;
        }
        case 0xb4: case 0xb5: {                            /* get/putfield */
            uint16_t idx = U2();
            const char *cn, *fn, *fd;
            jref o;
            jblk *b;
            jslot *f;
            uint32_t slot;
            bool is_ref;
            jslot val[2] = { { 0 }, { 0 } };
            if (cp_ref(cls, idx, &cn, &fn, &fd) != FX_OK)
                FAULT("bad field reference");
            /* putfield: [..., objectref, value] — pop value first */
            if (op == 0xb5) {
                if (fd[0] == 'J' || fd[0] == 'D') {
                    val[1].i = stack[--sp].i;
                    val[0].i = stack[--sp].i;
                } else {
                    val[0] = stack[--sp];
                }
            }
            o = stack[--sp].r;
            if (o == JREF_NULL) THROW("java/lang/NullPointerException", "null");
            b = heap_hdr(hp, o);
            f = (jslot *)heap_ptr(hp, o);
            if (!b->cls || class_find_field(b->cls, fn, fd, false, &slot,
                                            &is_ref) != FX_OK)
                FAULT("instance field not found");
            if (op == 0xb4) {
                if (fd[0] == 'J' || fd[0] == 'D')
                    PUSH_W(f[slot].i, f[slot + 1].i);
                else if (is_ref)
                    PUSH_R(f[slot].r);
                else if (fd[0] == 'F')
                    PUSH_F(f[slot].f);
                else
                    PUSH(f[slot].i);
            } else {
                if (fd[0] == 'J' || fd[0] == 'D') {
                    f[slot].i = val[0].i;
                    f[slot + 1].i = val[1].i;
                } else if (is_ref) {
                    f[slot].r = val[0].r;
                } else if (fd[0] == 'F') {
                    f[slot].f = val[0].f;
                } else {
                    f[slot].i = val[0].i;
                }
            }
            break;
        }

        /* --- method calls --------------------------------------------- */
        case 0xb6: case 0xb7: case 0xb8: case 0xb9: {
            uint16_t idx = U2();
            const char *cn, *mn, *md;
            jclass *tc, *tdecl;
            jmethod *tm;
            uint32_t n, i;
            jframe *nf;
            if (op == 0xb9)
                pc += 2;                       /* count, zero */
            if (cp_ref(cls, idx, &cn, &mn, &md) != FX_OK)
                FAULT("bad method reference");
            tc = vm_load_class(v, cn);
            if (!tc)
                FAULT(v->errbuf[0] ? v->errbuf : "class not found (invoke)");
            if (op == 0xb8) {
                ENSURE_INIT(tc);
            } else {
                jref recv = stack[sp - desc_arg_slots(md, false)].r;
                jblk *rb;
                if (recv == JREF_NULL) THROW("java/lang/NullPointerException", "null");
                rb = heap_hdr(hp, recv);
                if (op != 0xb7 || !rb->cls)
                    tc = rb->cls ? rb->cls : tc;
            }
            tdecl = tc;
            tm = class_find_method_decl(tc, mn, md, &tdecl);
            n = desc_arg_slots(md, op == 0xb8);
            if (!tm || !tm->code) {
                jslot r[2];
                const char *rt;
                int nr;
                r[0].i = 0;
                r[1].i = 0;
                nr = run_native(v, t, tc, mn, md, &stack[sp - (int)n], r);
                if (nr == FX_ERR_JAVA)
                    goto do_throw;          /* native raised an exception */
                if (nr != FX_OK) {
                    snprintf(v->errbuf, sizeof v->errbuf,
                             "missing native: %.48s.%.48s%.48s",
                             tc->name ? tc->name : "?", mn, md);
                    FAULT(v->errbuf);
                }
                sp -= (int)n;
                rt = strchr(md, ')') + 1;
                if (rt[0] != 'V') {
                    if (rt[0] == 'J' || rt[0] == 'D')
                        PUSH_W(r[0].i, r[1].i);
                    else if (rt[0] == 'L' || rt[0] == '[')
                        PUSH_R(r[0].r);
                    else
                        PUSH(r[0].i);
                }
                break;
            }
            if (sp < (int)n) FAULT("verify: stack underflow");
            if (t->nframes >= VM_MAX_FRAMES ||
                t->ltop + tm->max_locals > VM_LOCALS_SLOTS)
                FAULT("StackOverflowError");
            fr->pc = pc;                      /* save caller's resume point */
            nf = &t->frames[t->nframes];
            nf->m = tm;
            nf->cls = tdecl;
            nf->pc = tm->code;
            nf->caller_ipc = ipc;             /* for exception unwind match */
            nf->locals = &t->locals[t->ltop];
            nf->sp_base = sp - (int)n;
            memset(nf->locals, 0, sizeof(jslot) * (size_t)tm->max_locals);
            memset(&t->ltypes[t->ltop], 0, tm->max_locals);
            for (i = 0; i < n && i < tm->max_locals; i++) {
                nf->locals[i] = stack[nf->sp_base + (int)i];
                t->ltypes[t->ltop + i] = ttypes[nf->sp_base + (int)i];
            }
            t->ltop += tm->max_locals;
            t->nframes++;
            RELOAD();
            break;
        }

        /* --- object/array creation ------------------------------------ */
        case 0xbb: {                                     /* new */
            uint16_t idx = U2();
            const char *cn;
            jclass *nc;
            jref r;
            if (cp_class_name(cls, idx, &cn) != FX_OK)
                FAULT("bad class reference (new)");
            nc = vm_load_class(v, cn);
            if (!nc)
                FAULT(v->errbuf[0] ? v->errbuf : "class not found (new)");
            ENSURE_INIT(nc);
            r = vm_alloc(v, (size_t)nc->nslots * sizeof(jslot), nc, 0, 0);
            if (r == JREF_NULL) THROW("java/lang/OutOfMemoryError", "out of memory");
            PUSH_R(r);
            break;
        }
        case 0xbc: {                                     /* newarray */
            int atype = U1();
            int32_t n = stack[--sp].i;
            int esize = atype_size(atype);
            jref r;
            if (esize < 0) FAULT("bad newarray type");
            if (n < 0) THROW("java/lang/NegativeArraySizeException", "negative size");
            r = vm_alloc(v, (size_t)n * (size_t)esize, NULL, JBLK_ARRAY,
                         (uint32_t)n);
            if (r == JREF_NULL) THROW("java/lang/OutOfMemoryError", "out of memory");
            PUSH_R(r);
            break;
        }
        case 0xbd: {                                     /* anewarray */
            (void)U2();
            int32_t n = stack[--sp].i;
            jref r;
            if (n < 0) THROW("java/lang/NegativeArraySizeException", "negative size");
            r = vm_alloc(v, (size_t)n * 4, NULL, JBLK_ARRAY | JBLK_REFELEM,
                         (uint32_t)n);
            if (r == JREF_NULL) THROW("java/lang/OutOfMemoryError", "out of memory");
            PUSH_R(r);
            break;
        }
        case 0xbe: {                                     /* arraylength */
            jref a = stack[--sp].r;
            if (a == JREF_NULL) THROW("java/lang/NullPointerException", "null");
            PUSH((int32_t)heap_hdr(hp, a)->aux);
            break;
        }
        case 0xc5: {                                     /* multianewarray */
            uint16_t idx = U2();
            int dims = U1();
            const char *cn;
            int32_t counts[8];
            jref r;
            int i;
            if (dims < 1 || dims > 8) FAULT("bad multianewarray dims");
            if (cp_class_name(cls, idx, &cn) != FX_OK)
                FAULT("bad class reference (multianewarray)");
            for (i = dims - 1; i >= 0; i--)
                counts[i] = stack[--sp].i;
            r = alloc_multi(v, cn, counts, 0);
            if (r == JREF_NULL) THROW("java/lang/OutOfMemoryError", "out of memory");
            PUSH_R(r);
            break;
        }

        /* --- misc ----------------------------------------------------- */
        case 0xbf: {                                   /* athrow */
            jref e = stack[--sp].r;
            if (e == JREF_NULL)
                THROW("java/lang/NullPointerException", "athrow null");
            t->exception = e;
            goto do_throw;
        }
        case 0xc0: {                                     /* checkcast */
            uint16_t idx = U2();
            const char *cn;
            jref o = stack[sp - 1].r;    /* objectref stays on the stack */
            if (cp_class_name(cls, idx, &cn) != FX_OK)
                FAULT("bad class reference (checkcast)");
            if (o != JREF_NULL && !exc_isa(v, o, cn))
                THROW("java/lang/ClassCastException", cn);
            break;
        }
        case 0xc1: {                                     /* instanceof */
            uint16_t idx = U2();
            const char *cn;
            jref o = stack[--sp].r;
            jclass *tc, *cur;
            int hit = 0;
            if (cp_class_name(cls, idx, &cn) != FX_OK)
                FAULT("bad class reference (instanceof)");
            if (o != JREF_NULL) {
                jblk *b = heap_hdr(hp, o);
                if (b->cls) {
                    tc = vm_load_class(v, cn);
                    for (cur = b->cls; cur; cur = cur->super)
                        if (tc && strcmp(cur->name, tc->name) == 0)
                            hit = 1;
                }
            }
            PUSH(hit);
            break;
        }
        case 0xc2: case 0xc3:                           /* monitorenter/exit */
            (void)stack[--sp].r;
            break;
        case 0xc4: {                                     /* wide */
            uint8_t wop = U1();
            uint32_t ix = U2();
            CHK_LOCAL(ix);
            if (wop == 0x84) {
                int32_t cn = (int32_t)S2();
                locals[ix].i += cn;
                ltypes[ix] = 0;
            } else {
                switch (wop) {
                case 0x15: PUSH(locals[ix].i); break;
                case 0x16: CHK_LOCAL(ix + 1);
                           PUSH_W(locals[ix].i, locals[ix + 1].i); break;
                case 0x17: PUSH_F(locals[ix].f); break;
                case 0x18: CHK_LOCAL(ix + 1);
                           PUSH_W(locals[ix].i, locals[ix + 1].i); break;
                case 0x19: PUSH_R(locals[ix].r); break;
                case 0x36: locals[ix].i = stack[--sp].i; ltypes[ix] = 0; break;
                case 0x37: CHK_LOCAL(ix + 1);
                           locals[ix + 1].i = stack[--sp].i;
                           locals[ix].i = stack[--sp].i;
                           ltypes[ix] = 0; ltypes[ix + 1] = 2; break;
                case 0x38: locals[ix].f = stack[--sp].f; ltypes[ix] = 0; break;
                case 0x39: CHK_LOCAL(ix + 1);
                           locals[ix + 1].i = stack[--sp].i;
                           locals[ix].i = stack[--sp].i;
                           ltypes[ix] = 0; ltypes[ix + 1] = 2; break;
                case 0x3a: locals[ix].r = stack[--sp].r; ltypes[ix] = 1; break;
                case 0xa9: pc = code + locals[ix].i; break;
                default: FAULT("bad wide opcode");
                }
            }
            break;
        }
        case 0xba:                                     /* invokedynamic */
            FAULT("invokedynamic is not supported (Java 8+ class)");

        default:
            FAULT("unimplemented opcode");
        }
        continue;          /* normal instruction completion -> next opcode */

    /* Unwind: find a handler from the current frame upward. `ipc` is the
     * faulting instruction; on each pop, the match point in the caller is
     * the popped frame's caller_ipc (the invoke instruction). */
do_throw: {
        jref exc = t->exception;
        const uint8_t *at = ipc;
        if (exc == JREF_NULL)
            FAULT("athrow with no exception");
        for (;;) {
            int h = find_handler(v, cls, m, (uint32_t)(at - code), exc);
            if (h >= 0) {
                sp = fr->sp_base;             /* handler starts with exc only */
                stack[sp].r = exc;
                ttypes[sp] = 1;
                sp++;
                pc = code + h;
                goto dispatch;
            }
            if (t->nframes == base_depth + 1) {
                /* uncaught at this entry level: pop it and report */
                vm_exc_text(v, t, v->errbuf, sizeof v->errbuf);
                t->error = v->errbuf;
                t->ltop -= m->max_locals;
                t->nframes--;
                return FX_ERR_JAVA;
            }
            at = fr->caller_ipc;              /* invoke site in the caller */
            t->ltop -= m->max_locals;
            t->nframes--;
            RELOAD();
        }
    }
    }
}
