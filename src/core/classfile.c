/* fxj2me: .class file parser. Big-endian reading with strict bounds checks. */
#include "classfile.h"
#include "romclass.h"
#include <stdlib.h>
#include <string.h>

typedef struct cursor {
    const uint8_t *p, *end;
    bool bad;
} cursor;

static uint8_t u1(cursor *c)
{
    if (c->bad || c->p + 1 > c->end) { c->bad = true; return 0; }
    return *c->p++;
}

static uint16_t u2(cursor *c)
{
    uint16_t v;
    if (c->bad || c->p + 2 > c->end) { c->bad = true; return 0; }
    v = (uint16_t)((c->p[0] << 8) | c->p[1]);
    c->p += 2;
    return v;
}

static uint32_t u4(cursor *c)
{
    uint32_t v;
    if (c->bad || c->p + 4 > c->end) { c->bad = true; return 0; }
    v = ((uint32_t)c->p[0] << 24) | ((uint32_t)c->p[1] << 16) |
        ((uint32_t)c->p[2] << 8) | (uint32_t)c->p[3];
    c->p += 4;
    return v;
}

static void skip(cursor *c, size_t n)
{
    if (c->bad || c->p + n > c->end) { c->bad = true; return; }
    c->p += n;
}

/* NUL-terminated copy of a cp UTF-8 entry. `st` (optional) separates a
 * malformed index/tag (FX_ERR_FORMAT) from a failed allocation
 * (FX_ERR_NOSPACE) so callers can report the real cause. */
/* Resolve a UTF-8 pool entry to its string. No copy is made: the string is
 * owned by the class's string table (or, once loaded into a VM, shared with
 * the VM's intern table), so method/field names and descriptors never
 * duplicate constant-pool text. */
static const char *cp_utf8_dup(const jclass *cl, uint16_t idx, int *st)
{
    const cp_entry *e;

    if (st)
        *st = FX_OK;
    if (idx == 0 || idx >= cl->cp_count)
        e = NULL;
    else
        e = &cl->cp[idx];
    if (!e || e->tag != CP_UTF8 || e->v >= (uint32_t)cl->nstrs) {
        if (st)
            *st = FX_ERR_FORMAT;
        return NULL;
    }
    return cl->strs[e->v];
}

uint32_t desc_arg_slots(const char *desc, bool is_static)
{
    uint32_t slots = is_static ? 0 : 1;
    const char *p = desc;

    if (!p || *p != '(')
        return slots;
    for (p++; *p && *p != ')'; p++) {
        while (*p == '[')
            p++;
        if (*p == 'L') {
            while (*p && *p != ';')
                p++;
        } else if (*p == 'J' || *p == 'D') {
            slots++;
        }
        slots++;
    }
    return slots;
}

static int parse_cp(cursor *c, jclass *cl)
{
    int count = u2(c);
    int i;

    if (c->bad || count < 1)
        return FX_ERR_FORMAT;
    cl->cp = calloc((size_t)count, sizeof(cp_entry));
    if (!cl->cp)
        return FX_ERR_NOSPACE;
    cl->cp_count = count;

    for (i = 1; i < count; i++) {
        cp_entry *e = &cl->cp[i];
        e->tag = u1(c);
        switch (e->tag) {
        case CP_UTF8: {
            uint16_t len = u2(c);
            char *s;
            if (c->bad || c->p + len > c->end)
                return FX_ERR_FORMAT;
            if (cl->nstrs == cl->strcap) {
                int cap = cl->strcap ? cl->strcap * 2 : 32;
                char **t = realloc(cl->strs, (size_t)cap * sizeof *t);
                if (!t)
                    return FX_ERR_NOSPACE;
                cl->strs = t;
                cl->strcap = cap;
            }
            s = malloc((size_t)len + 1);
            if (!s)
                return FX_ERR_NOSPACE;
            memcpy(s, c->p, len);
            s[len] = 0;
            e->v = (uint32_t)cl->nstrs;
            cl->strs[cl->nstrs++] = s;
            e->a = (uint8_t)(len & 0xff);
            e->b = (uint8_t)(len >> 8);
            skip(c, len);
            break;
        }
        case CP_INTEGER:
        case CP_FLOAT:
            e->v = u4(c);
            break;
        case CP_LONG:
        case CP_DOUBLE: {
            uint32_t hi, lo;
            hi = u4(c);
            lo = u4(c);
            e->v = lo;
            i++;                    /* long/double take two cp slots */
            if (i >= count)
                return FX_ERR_FORMAT;
            cl->cp[i].v = hi;       /* high half lives in the spare slot */
            break;
        }
        case CP_CLASS:
        case CP_STRING:
        case CP_METHODTYPE:
            e->v = u2(c);
            break;
        case CP_FIELDREF:
        case CP_METHODREF:
        case CP_IFMETHODREF:
        case CP_NAMEANDTYPE:
        case CP_DYNAMIC:
        case CP_INVOKEDYNAMIC: {
            uint32_t x = u2(c), y = u2(c);
            e->v = (x << 16) | (y & 0xffffu);
            break;
        }
        case CP_METHODHANDLE:
            u1(c);
            e->v = u2(c);
            break;
        default:
            return FX_ERR_FORMAT;
        }
        if (c->bad)
            return FX_ERR_FORMAT;
    }
    return FX_OK;
}

/* Split ACC_STATIC fields out of cl->fields into cl->statics. */
static int split_statics(jclass *cl)
{
    int i, ns = 0, ni = 0;
    jfield *sf, *inf;

    for (i = 0; i < cl->nfields; i++)
        if (cl->fields[i].access & ACC_STATIC)
            ns++;
    ni = cl->nfields - ns;
    if (ns == 0)
        return FX_OK;

    sf = calloc((size_t)ns, sizeof(jfield));
    inf = calloc((size_t)(ni ? ni : 1), sizeof(jfield));
    if (!sf || !inf) {
        free(sf);
        free(inf);
        return FX_ERR_NOSPACE;
    }
    ns = ni = 0;
    for (i = 0; i < cl->nfields; i++) {
        if (cl->fields[i].access & ACC_STATIC)
            sf[ns++] = cl->fields[i];
        else
            inf[ni++] = cl->fields[i];
    }
    free(cl->fields);
    cl->fields = inf;
    cl->nfields = ni;
    cl->statics = sf;
    cl->nstatics = ns;
    return FX_OK;
}

static int parse_fields(cursor *c, jclass *cl)
{
    int count = u2(c);
    int i;

    if (c->bad || count < 0)
        return FX_ERR_FORMAT;
    if (count) {
        cl->fields = calloc((size_t)count, sizeof(jfield));
        if (!cl->fields)
            return FX_ERR_NOSPACE;
    }
    cl->nfields = count;

    for (i = 0; i < count; i++) {
        jfield *f = &cl->fields[i];
        uint16_t name_idx, desc_idx;
        int nattr, a, st;

        f->access = u2(c);
        name_idx = u2(c);
        desc_idx = u2(c);
        nattr = u2(c);
        if (c->bad)
            return FX_ERR_FORMAT;
        f->name = cp_utf8_dup(cl, name_idx, &st);
        f->desc = st == FX_OK ? cp_utf8_dup(cl, desc_idx, &st) : NULL;
        if (!f->name || !f->desc)
            return st;
        f->is_ref = (f->desc[0] == 'L' || f->desc[0] == '[');
        for (a = 0; a < nattr; a++) {
            uint32_t alen;
            u2(c);
            alen = u4(c);
            skip(c, alen);
        }
        if (c->bad)
            return FX_ERR_FORMAT;
    }
    return split_statics(cl);
}

static int parse_methods(cursor *c, jclass *cl)
{
    int count = u2(c);
    int i;

    if (c->bad || count < 0)
        return FX_ERR_FORMAT;
    if (count) {
        cl->methods = calloc((size_t)count, sizeof(jmethod));
        if (!cl->methods)
            return FX_ERR_NOSPACE;
    }
    cl->nmethods = count;

    for (i = 0; i < count; i++) {
        jmethod *m = &cl->methods[i];
        uint16_t name_idx, desc_idx;
        int nattr, a, st;

        m->access = u2(c);
        name_idx = u2(c);
        desc_idx = u2(c);
        nattr = u2(c);
        if (c->bad)
            return FX_ERR_FORMAT;
        m->name = cp_utf8_dup(cl, name_idx, &st);
        m->desc = st == FX_OK ? cp_utf8_dup(cl, desc_idx, &st) : NULL;
        if (!m->name || !m->desc)
            return st;
        m->narg_slots = desc_arg_slots(m->desc, (m->access & ACC_STATIC) != 0);

        for (a = 0; a < nattr; a++) {
            uint16_t aname = u2(c);
            uint32_t alen = u4(c);
            const uint8_t *astart = c->p;

            if (c->bad || c->p + alen > c->end)
                return FX_ERR_FORMAT;

            {
                const char *aname_s = cp_utf8_dup(cl, aname, NULL);
                bool is_code = aname_s && strcmp(aname_s, "Code") == 0;
                if (is_code) {
                    cursor cc = { c->p, c->p + alen, false };
                    int nx;

                    m->max_stack = u2(&cc);
                    m->max_locals = u2(&cc);
                    m->code_len = u4(&cc);
                    if (cc.bad || cc.p + m->code_len > cc.end)
                        return FX_ERR_FORMAT;
                    m->code = cc.p;
                    skip(&cc, m->code_len);
                    nx = u2(&cc);
                    if (cc.bad || nx < 0 || cc.p + (size_t)nx * 8 > cc.end)
                        return FX_ERR_FORMAT;
                    m->exc_table = cc.p;
                    m->exc_len = (uint16_t)nx;
                    skip(&cc, (size_t)nx * 8);
                    /* ignore remaining sub-attributes */
                }
            }
            c->p = astart + alen;
        }
        if (c->bad)
            return FX_ERR_FORMAT;
    }
    return FX_OK;
}

/* Point every method/field reference from `from` to `to`. */
static void repoint_string(jclass *cl, const char *from, const char *to)
{
    int i;

    for (i = 0; i < cl->nmethods; i++) {
        if (cl->methods[i].name == from)
            cl->methods[i].name = to;
        if (cl->methods[i].desc == from)
            cl->methods[i].desc = to;
    }
    for (i = 0; i < cl->nfields; i++) {
        if (cl->fields[i].name == from)
            cl->fields[i].name = to;
        if (cl->fields[i].desc == from)
            cl->fields[i].desc = to;
    }
    for (i = 0; i < cl->nstatics; i++) {
        if (cl->statics[i].name == from)
            cl->statics[i].name = to;
        if (cl->statics[i].desc == from)
            cl->statics[i].desc = to;
    }
}

const char *rom_intern(const char *s)
{
    uint32_t h = 2166136261u, i, n;
    const char *p;

    if (!fx_rom_nstrs)
        return NULL;
    for (p = s; *p; p++) {
        h ^= (unsigned char)*p;
        h *= 16777619u;
    }
    i = h & (FX_ROM_STR_SLOTS - 1);
    for (n = 0; n < FX_ROM_STR_SLOTS; n++) {
        uint16_t k = fx_rom_str_hash[i];
        if (k == 0xffffu)
            return NULL;
        if (k < fx_rom_nstrs && strcmp(fx_rom_strs[k], s) == 0)
            return fx_rom_strs[k];
        i = (i + 1) & (FX_ROM_STR_SLOTS - 1);
    }
    return NULL;
}

/*--- constant-pool compaction -----------------------------------------*
 *
 * javac writes a pool entry for every class, field and method a class file
 * mentions, but a MIDlet only touches a fraction of them at runtime (the rest
 * exist for linking, which is done from the parsed tables). Dropping the
 * entries no instruction refers to is worth more than a third of the pool on
 * a real game, and the pool is 8 bytes an entry on a ~35 KB machine.
 *
 * The rewrite is stable (entries keep their relative order), so a remapped
 * index is never larger than the original — `ldc`'s one-byte operand stays
 * valid without promotion to `ldc_w`, and no code offset moves.
 */

static uint32_t be_u2(const uint8_t *p)
{
    return ((uint32_t)p[0] << 8) | p[1];
}

static uint32_t be_u4(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

/* Instruction length at `p`; *cpw is the width of a pool operand (0 = none,
 * 1 = ldc's byte, 2 = u2). 0 means "malformed, stop walking". */
static uint32_t insn_len(const uint8_t *base, const uint8_t *p,
                         const uint8_t *end, uint32_t *cpw)
{
    uint8_t op = *p;
    uint32_t off = (uint32_t)(p - base);

    *cpw = 0;
    if (op == 0x12)
        return *cpw = 1, 2;                       /* ldc */
    if (op == 0x13 || op == 0x14)
        return *cpw = 2, 3;                       /* ldc_w, ldc2_w */
    if (op == 0x11 || op == 0x84)
        return 3;                                 /* sipush, iinc */
    if (op == 0x10 || op == 0xa9 || op == 0xbc)
        return 2;                                 /* bipush, ret, newarray */
    if ((op >= 0x15 && op <= 0x19) || (op >= 0x36 && op <= 0x3a))
        return 2;                                 /* xload, xstore */
    if ((op >= 0x99 && op <= 0xa8) || op == 0xc6 || op == 0xc7)
        return 3;                                 /* branches */
    if (op == 0xc8 || op == 0xc9)
        return 5;                                 /* goto_w, jsr_w */
    if (op >= 0xb2 && op <= 0xb8)
        return *cpw = 2, 3;                       /* get/putfield, invoke* */
    if (op == 0xb9 || op == 0xba)
        return *cpw = 2, 5;                       /* invokeinterface, indy */
    if (op == 0xbb || op == 0xbd || op == 0xc0 || op == 0xc1)
        return *cpw = 2, 3;                       /* new, newarray, casts */
    if (op == 0xc5)
        return *cpw = 2, 4;                       /* multianewarray */
    if (op == 0xc4)                               /* wide */
        return p[1] == 0x84 ? 6 : 4;
    if (op == 0xaa) {                             /* tableswitch */
        uint32_t q = (off + 4u) & ~3u;
        int32_t lo, hi;
        if ((size_t)q + 12 > (size_t)(end - base))
            return 0;
        lo = (int32_t)be_u4(base + q + 4);
        hi = (int32_t)be_u4(base + q + 8);
        if (hi < lo)
            return 0;
        return q + 12 + 4u * (uint32_t)(hi - lo + 1);
    }
    if (op == 0xab) {                             /* lookupswitch */
        uint32_t q = (off + 4u) & ~3u;
        int32_t n;
        if ((size_t)q + 8 > (size_t)(end - base))
            return 0;
        n = (int32_t)be_u4(base + q + 4);
        if (n < 0)
            return 0;
        return q + 8 + 8u * (uint32_t)n;
    }
    return 1;
}

/* Mark (used != NULL) or rewrite (remap != NULL) every pool reference in a
 * method body. Operand-first layout means the index is always p[1..2]. */
static void walk_code(uint8_t *code, uint32_t len, uint8_t *used,
                      const uint16_t *remap, int cp_count)
{
    const uint8_t *base = code, *end = code + len, *p = code;

    while (p < end) {
        uint32_t cpw, n = insn_len(base, p, end, &cpw), idx;

        if (!n || (size_t)n > (size_t)(end - p))
            return;
        if (cpw) {
            idx = cpw == 1 ? p[1] : be_u2(p + 1);
            if (idx && idx < (uint32_t)cp_count) {
                if (used)
                    used[idx] = 1;
                else if (cpw == 1)
                    code[p - base + 1] = (uint8_t)remap[idx];
                else {
                    uint16_t nw = remap[idx];
                    code[p - base + 1] = (uint8_t)(nw >> 8);
                    code[p - base + 2] = (uint8_t)(nw & 0xff);
                }
            }
        }
        p += n;
    }
}

/* catch_type is the last u2 of each 8-byte exception-table entry. */
static void walk_exc(uint8_t *exc, uint16_t n, uint8_t *used,
                     const uint16_t *remap, int cp_count)
{
    uint16_t i;
    for (i = 0; i < n; i++) {
        uint8_t *e = exc + (size_t)i * 8;
        uint32_t ct = be_u2(e + 6);
        if (!ct || ct >= (uint32_t)cp_count)
            continue;
        if (used)
            used[ct] = 1;
        else {
            e[6] = (uint8_t)(remap[ct] >> 8);
            e[7] = (uint8_t)(remap[ct] & 0xff);
        }
    }
}

/* An entry kept by the bytecode pulls in the entries its operands name. */
static void mark_closure(jclass *cl, uint8_t *used)
{
    int changed = 1;

    while (changed) {
        int i;
        changed = 0;
        for (i = 1; i < cl->cp_count; i++) {
            cp_entry *e = &cl->cp[i];
            uint32_t refs[2];
            int n = 0, k;

            if (!used[i])
                continue;
            switch (e->tag) {
            case CP_CLASS:
            case CP_STRING:
            case CP_METHODTYPE:
                refs[n++] = e->v;
                break;
            case CP_METHODHANDLE:
                refs[n++] = e->v;
                break;
            case CP_FIELDREF:
            case CP_METHODREF:
            case CP_IFMETHODREF:
            case CP_NAMEANDTYPE:
                refs[n++] = e->v >> 16;
                refs[n++] = e->v & 0xffffu;
                break;
            case CP_DYNAMIC:
            case CP_INVOKEDYNAMIC:
                refs[n++] = e->v & 0xffffu;   /* hi half is not a pool ref */
                break;
            case CP_LONG:
            case CP_DOUBLE:
                if (i + 1 < cl->cp_count && !used[i + 1]) {
                    used[i + 1] = 1;          /* the spare (high) half */
                    changed = 1;
                }
                break;
            default:
                break;
            }
            for (k = 0; k < n; k++) {
                uint32_t r = refs[k];
                if (r && r < (uint32_t)cl->cp_count && !used[r]) {
                    used[r] = 1;
                    changed = 1;
                }
            }
        }
    }
}

static void remap_cp_operands(jclass *cl, const uint16_t *remap)
{
    int i;
    for (i = 1; i < cl->cp_count; i++) {
        cp_entry *e = &cl->cp[i];
        uint32_t a, b;

        switch (e->tag) {
        case CP_CLASS:
        case CP_STRING:
        case CP_METHODTYPE:
        case CP_METHODHANDLE:
            if (e->v < (uint32_t)cl->cp_count)
                e->v = remap[e->v];
            break;
        case CP_FIELDREF:
        case CP_METHODREF:
        case CP_IFMETHODREF:
        case CP_NAMEANDTYPE:
            a = e->v >> 16;
            b = e->v & 0xffffu;
            if (a < (uint32_t)cl->cp_count)
                a = remap[a];
            if (b < (uint32_t)cl->cp_count)
                b = remap[b];
            e->v = (a << 16) | (b & 0xffffu);
            break;
        case CP_DYNAMIC:
        case CP_INVOKEDYNAMIC:
            b = e->v & 0xffffu;
            if (b < (uint32_t)cl->cp_count)
                b = remap[b];
            e->v = (e->v & 0xffff0000u) | (b & 0xffffu);
            break;
        default:
            break;
        }
    }
}

/* Old javac emitted `jsr` subroutines, whose bodies are not reachable by a
 * linear walk; a miss would drop a live entry, so such classes keep their
 * pool (a raw byte scan is enough: a false positive only skips compaction). */
static bool has_jsr(const jclass *cl)
{
    int i;
    for (i = 0; i < cl->nmethods; i++) {
        const uint8_t *c = cl->methods[i].code;
        const uint8_t *end = c + cl->methods[i].code_len;
        const uint8_t *p = c;
        if (!c)
            continue;
        while (p < end) {
            uint32_t cpw, n = insn_len(c, p, end, &cpw);
            if (*p == 0xa8 || *p == 0xa9 || *p == 0xc9)
                return true;
            if (!n || (size_t)n > (size_t)(end - p))
                break;
            p += n;
        }
    }
    return false;
}

static int compact_cp(jclass *cl)
{
    uint8_t *used;
    uint16_t *remap;
    int i, w = 0;

    if (cl->cp_count <= 1 || has_jsr(cl))
        return FX_OK;
    used = calloc((size_t)cl->cp_count, 1);
    if (!used)
        return FX_ERR_NOSPACE;
    used[0] = 1;
    for (i = 0; i < cl->nmethods; i++) {
        jmethod *m = &cl->methods[i];
        if (m->code && m->code_len)
            walk_code((uint8_t *)(void *)m->code, m->code_len, used, NULL,
                      cl->cp_count);
        if (m->exc_table && m->exc_len)
            walk_exc((uint8_t *)(void *)m->exc_table, m->exc_len, used, NULL,
                     cl->cp_count);
    }
    mark_closure(cl, used);

    remap = malloc((size_t)cl->cp_count * sizeof *remap);
    if (!remap) {
        free(used);
        return FX_ERR_NOSPACE;
    }
    for (i = 0; i < cl->cp_count; i++) {
        if (!used[i]) {
            remap[i] = 0;
            continue;
        }
        remap[i] = (uint16_t)w++;
        if ((cl->cp[i].tag == CP_LONG || cl->cp[i].tag == CP_DOUBLE) &&
            i + 1 < cl->cp_count) {              /* keep the pair adjacent */
            used[i + 1] = 1;
            remap[i + 1] = (uint16_t)w++;
            i++;
        }
    }

    for (i = 0; i < cl->nmethods; i++) {
        jmethod *m = &cl->methods[i];
        if (m->code && m->code_len)
            walk_code((uint8_t *)(void *)m->code, m->code_len, NULL, remap,
                      cl->cp_count);
        if (m->exc_table && m->exc_len)
            walk_exc((uint8_t *)(void *)m->exc_table, m->exc_len, NULL, remap,
                     cl->cp_count);
    }
    remap_cp_operands(cl, remap);

    /* move the live entries down (new index <= old, so this is in place) */
    w = 0;
    for (i = 0; i < cl->cp_count; i++) {
        if (!used[i])
            continue;
        if (w != i)
            cl->cp[w] = cl->cp[i];
        w++;
    }
    cl->cp_count = w;
    free(used);
    free(remap);
    return FX_OK;
}

/* Pack the class's strings into a single block: thousands of tiny mallocs
 * cost a block header each and fragment a small pool. Tables and methods
 * point into the block afterwards.
 *
 * Text the boot image already holds is not copied at all: the entry points
 * into flash instead. On a real MIDlet that is most of the pool text
 * (descriptors, class and member names), because it talks to java.* and
 * javax.microedition.* the whole time. */
static int compact_strings(jclass *cl)
{
    uint32_t total = 0, off = 0;
    char *blob;
    int i;

    for (i = 0; i < cl->nstrs; i++) {
        if (!rom_intern(cl->strs[i]))
            total += (uint32_t)strlen(cl->strs[i]) + 1;
    }
    if (!total)
        total = 1;                  /* every string is shared: no blob */
    blob = malloc(total);
    if (!blob)
        return FX_ERR_NOSPACE;
    for (i = 0; i < cl->nstrs; i++) {
        char *old = cl->strs[i];
        const char *hit = rom_intern(old);
        size_t n;

        if (hit) {
            repoint_string(cl, old, (char *)(void *)hit);
            free(old);
            cl->strs[i] = (char *)(void *)hit;
            continue;
        }
        n = strlen(old) + 1;
        memcpy(blob + off, old, n);
        repoint_string(cl, old, blob + off);
        free(old);
        cl->strs[i] = blob + off;
        off += (uint32_t)n;
    }
    cl->strblob = blob;
    /* the table only needs one entry per pool string; parse grew it by
     * doubling, which on a 4-byte-pointer target wastes ~1k per class */
    if (cl->nstrs) {
        char **t = realloc(cl->strs, (size_t)cl->nstrs * sizeof *t);
        if (t) {
            cl->strs = t;
            cl->strcap = cl->nstrs;
        }
    }
    return FX_OK;
}

/* Pack every method's Code bytes and exception table into one buffer, then
 * drop the class file itself: its constant-pool text and attribute headers
 * are pure overhead once parsed (on KoalaMines they are 42k of 54k). */
static int extract_code(jclass *cl)
{
    uint32_t total = 0, off = 0;
    uint8_t *buf;
    int i;

    for (i = 0; i < cl->nmethods; i++) {
        total += cl->methods[i].code_len;
        total += (uint32_t)cl->methods[i].exc_len * 8;
    }
    if (total) {
        buf = malloc(total);
        if (!buf)
            return FX_ERR_NOSPACE;
        for (i = 0; i < cl->nmethods; i++) {
            jmethod *m = &cl->methods[i];
            if (m->code_len) {
                memcpy(buf + off, m->code, m->code_len);
                m->code = buf + off;
                off += m->code_len;
            }
            if (m->exc_len) {
                memcpy(buf + off, m->exc_table, (size_t)m->exc_len * 8);
                m->exc_table = buf + off;
                off += (uint32_t)m->exc_len * 8;
            }
        }
        cl->codes = buf;
        cl->codes_len = off;
    }
    free(cl->bytes);
    cl->bytes = NULL;
    cl->nbytes = 0;
    return FX_OK;
}

int class_parse(uint8_t *bytes, size_t len, jclass **out)
{
    cursor c = { bytes, bytes + len, false };
    jclass *cl = calloc(1, sizeof *cl);
    uint32_t magic;
    uint16_t this_idx, super_idx;
    int niface, i, nattr;

    *out = NULL;
    if (!cl) {
        free(bytes);
        return FX_ERR_NOSPACE;
    }
    cl->bytes = bytes;
    cl->nbytes = len;

    magic = u4(&c);
    if (magic != 0xCAFEBABEu) {
        class_free(cl);
        return FX_ERR_FORMAT;
    }
    u2(&c);                     /* minor */
    u2(&c);                     /* major: accept 45..modern */

    {
        int st = parse_cp(&c, cl);
        if (st != FX_OK) {
            class_free(cl);
            return st;          /* keep "out of memory" distinct from format */
        }
    }
    cl->access = u2(&c);
    this_idx = u2(&c);
    super_idx = u2(&c);
    if (c.bad) {
        class_free(cl);
        return FX_ERR_FORMAT;
    }

    {
        const char *n = NULL;
        if (cp_class_name(cl, this_idx, &n) != FX_OK) {
            class_free(cl);
            return FX_ERR_FORMAT;
        }
        cl->name = malloc(strlen(n) + 1);
        if (!cl->name) { class_free(cl); return FX_ERR_NOSPACE; }
        strcpy(cl->name, n);
    }
    if (super_idx) {
        const char *n = NULL;
        if (cp_class_name(cl, super_idx, &n) != FX_OK) {
            class_free(cl);
            return FX_ERR_FORMAT;
        }
        cl->super_name = malloc(strlen(n) + 1);
        if (!cl->super_name) { class_free(cl); return FX_ERR_NOSPACE; }
        strcpy(cl->super_name, n);
    }

    niface = u2(&c);
    if (c.bad || niface < 0) { class_free(cl); return FX_ERR_FORMAT; }
    for (i = 0; i < niface; i++)
        u2(&c);

    {
        int st = parse_fields(&c, cl);
        if (st == FX_OK)
            st = parse_methods(&c, cl);
        if (st != FX_OK) {
            class_free(cl);
            return st;          /* a failed allocation is not bad bytecode */
        }
    }

    nattr = u2(&c);
    if (c.bad || nattr < 0) { class_free(cl); return FX_ERR_FORMAT; }
    for (i = 0; i < nattr; i++) {
        uint32_t alen;
        u2(&c);
        alen = u4(&c);
        skip(&c, alen);
    }
    if (c.bad) { class_free(cl); return FX_ERR_FORMAT; }

    {
        int st = compact_cp(cl);
        if (st == FX_OK)
            st = compact_strings(cl);
        if (st == FX_OK)
            st = extract_code(cl);
        if (st != FX_OK) {
            class_free(cl);
            return st;
        }
    }

    *out = cl;
    return FX_OK;
}

void class_free(jclass *cl)
{
    int i;
    if (!cl)
        return;
    if (cl->rom) {
        /* Only the header and the static storage were allocated; every
         * table points into the flash image. */
        free(cl->static_vals);
        free(cl);
        return;
    }
    free(cl->cp);
    /* Method/field names and descriptors live in the string block, so there
     * are no per-string frees. */
    if (cl->strblob) {
        free(cl->strblob);
    } else {
        for (i = 0; i < cl->nstrs; i++)
            free(cl->strs[i]);
    }
    free(cl->name);
    free(cl->super_name);
    free(cl->strs);
    free(cl->codes);
    free(cl->fields);
    free(cl->methods);
    free(cl->statics);
    free(cl->refmask);
    free(cl->srefmask);
    free(cl->static_vals);
    free(cl->bytes);
    free(cl);
}

int class_link(jclass *cl, jclass *(*resolve)(const char *name, void *ud),
               void *ud)
{
    uint32_t slot = 0, sslot = 0;
    int i, nwords;

    if (cl->linked)
        return FX_OK;

    /* A ROM image was linked at build time: slots, refmasks and static
     * counts are already final, so only the super chain needs wiring. */
    if (cl->rom) {
        if (cl->super_name) {
            cl->super = resolve(cl->super_name, ud);
            if (!cl->super)
                return FX_ERR_NOTFOUND;
            if (!cl->super->linked &&
                class_link(cl->super, resolve, ud) != FX_OK)
                return FX_ERR_FORMAT;
        }
        cl->linked = true;
        return FX_OK;
    }

    /* Always link the superclass, including java/lang/Object: without the
     * chain pointer, virtual dispatch to Object methods and native lookup
     * through the hierarchy break. */
    if (cl->super_name) {
        cl->super = resolve(cl->super_name, ud);
        if (!cl->super)
            return FX_ERR_NOTFOUND;
        if (!cl->super->linked && class_link(cl->super, resolve, ud) != FX_OK)
            return FX_ERR_FORMAT;
        slot = cl->super->nslots;
    }

    /* Instance fields: slots continue the super chain. */
    for (i = 0; i < cl->nfields; i++) {
        jfield *f = &cl->fields[i];
        f->slot = slot;
        slot += (f->desc[0] == 'J' || f->desc[0] == 'D') ? 2 : 1;
    }
    cl->nslots = slot;
    nwords = (int)((slot + 31) / 32);
    cl->refmask = calloc(nwords ? nwords : 1, sizeof(uint32_t));
    if (!cl->refmask)
        return FX_ERR_NOSPACE;
    if (cl->super && cl->super->refmask)
        memcpy(cl->refmask, cl->super->refmask,
               (size_t)nwords * sizeof(uint32_t));
    for (i = 0; i < cl->nfields; i++) {
        jfield *f = &cl->fields[i];
        if (f->is_ref)
            cl->refmask[f->slot / 32] |= 1u << (f->slot % 32);
    }

    /* Statics: separate slot space, storage allocated at link. */
    for (i = 0; i < cl->nstatics; i++) {
        jfield *f = &cl->statics[i];
        f->slot = sslot;
        sslot += (f->desc[0] == 'J' || f->desc[0] == 'D') ? 2 : 1;
    }
    cl->nstat_slots = sslot;
    nwords = (int)((sslot + 31) / 32);
    cl->srefmask = calloc(nwords ? nwords : 1, sizeof(uint32_t));
    cl->static_vals = calloc(sslot ? sslot : 1, sizeof(jslot));
    if (!cl->srefmask || !cl->static_vals)
        return FX_ERR_NOSPACE;
    for (i = 0; i < cl->nstatics; i++) {
        jfield *f = &cl->statics[i];
        if (f->is_ref)
            cl->srefmask[f->slot / 32] |= 1u << (f->slot % 32);
    }

    cl->linked = true;
    return FX_OK;
}

jmethod *class_find_method_decl(jclass *cl, const char *name,
                                const char *desc, jclass **decl)
{
    for (; cl; cl = cl->super) {
        int i;
        for (i = 0; i < cl->nmethods; i++)
            if (strcmp(cl->methods[i].name, name) == 0 &&
                strcmp(cl->methods[i].desc, desc) == 0) {
                if (decl)
                    *decl = cl;
                return &cl->methods[i];
            }
    }
    return NULL;
}

jmethod *class_find_method(jclass *cl, const char *name, const char *desc)
{
    return class_find_method_decl(cl, name, desc, NULL);
}

int class_find_field(jclass *cl, const char *name, const char *desc,
                     bool is_static, uint32_t *slot, bool *is_ref)
{
    for (; cl; cl = cl->super) {
        int i;
        jfield *list = is_static ? cl->statics : cl->fields;
        int n = is_static ? cl->nstatics : cl->nfields;
        for (i = 0; i < n; i++)
            if (strcmp(list[i].name, name) == 0 &&
                strcmp(list[i].desc, desc) == 0) {
                if (slot) *slot = list[i].slot;
                if (is_ref) *is_ref = list[i].is_ref;
                return FX_OK;
            }
    }
    return FX_ERR_NOTFOUND;
}

/*--- constant pool helpers ---------------------------------------------*/

int32_t cp_ival(const cp_entry *e)
{
    return (int32_t)e->v;
}

float cp_fval(const cp_entry *e)
{
    uint32_t bits = e->v;
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

int64_t cp_lval(const jclass *cl, int idx)
{
    uint32_t lo = cl->cp[idx].v;
    uint32_t hi = (idx + 1 < cl->cp_count) ? cl->cp[idx + 1].v : 0;
    return ((int64_t)hi << 32) | lo;
}

double cp_dval(const jclass *cl, int idx)
{
    int64_t bits = cp_lval(cl, idx);
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

static int cp_utf8_str(const jclass *cl, uint16_t idx, const char **s)
{
    const cp_entry *e;
    if (idx <= 0 || idx >= cl->cp_count)
        return FX_ERR_FORMAT;
    e = &cl->cp[idx];
    if (e->tag != CP_UTF8 || e->v >= (uint32_t)cl->nstrs)
        return FX_ERR_FORMAT;
    *s = cl->strs[e->v];
    return FX_OK;
}

int cp_class_name(const jclass *cl, uint16_t idx, const char **name)
{
    if (idx <= 0 || idx >= cl->cp_count || cl->cp[idx].tag != CP_CLASS)
        return FX_ERR_FORMAT;
    return cp_utf8_str(cl, (uint16_t)cl->cp[idx].v, name);
}

int cp_string_utf8(const jclass *cl, uint16_t idx, const uint8_t **p,
                   uint16_t *len)
{
    const cp_entry *e;
    if (idx <= 0 || idx >= cl->cp_count || cl->cp[idx].tag != CP_STRING)
        return FX_ERR_FORMAT;
    idx = (uint16_t)cl->cp[idx].v;
    if (idx <= 0 || idx >= cl->cp_count)
        return FX_ERR_FORMAT;
    e = &cl->cp[idx];
    if (e->tag != CP_UTF8 || e->v >= (uint32_t)cl->nstrs)
        return FX_ERR_FORMAT;
    *p = (const uint8_t *)cl->strs[e->v];
    *len = (uint16_t)(e->a | (e->b << 8));
    return FX_OK;
}

/*--- boot ROM image ---------------------------------------------------*/

int rom_find(const char *name)
{
    int i;
    for (i = 0; i < fx_rom_nclasses; i++)
        if (strcmp(fx_rom_classes[i].name, name) == 0)
            return i;
    return -1;
}

jclass *class_from_rom(int idx)
{
    const fx_rom_class *img;
    jclass *cl;

    if (idx < 0 || idx >= fx_rom_nclasses)
        return NULL;
    img = &fx_rom_classes[idx];
    cl = calloc(1, sizeof *cl);
    if (!cl)
        return NULL;
    cl->rom = true;
    /* Everything below is flash: the casts drop const because jclass's
     * fields are shared with parsed classes, and the rom flag keeps every
     * writer and the free path away from them. */
    cl->name = (char *)(void *)img->name;
    cl->super_name = (char *)(void *)img->super_name;
    cl->access = img->access;
    cl->cp = (cp_entry *)(void *)img->cp;
    cl->cp_count = img->cp_count;
    cl->strs = (char **)(void *)img->strs;
    cl->nstrs = img->nstrs;
    cl->strblob = (char *)(void *)img->strblob;
    cl->methods = (jmethod *)(void *)img->methods;
    cl->nmethods = img->nmethods;
    cl->fields = (jfield *)(void *)img->fields;
    cl->nfields = img->nfields;
    cl->nslots = img->nslots;
    cl->refmask = (uint32_t *)(void *)img->refmask;
    cl->statics = (jfield *)(void *)img->statics;
    cl->nstatics = img->nstatics;
    cl->nstat_slots = img->nstat_slots;
    cl->srefmask = (uint32_t *)(void *)img->srefmask;
    if (img->nstat_slots) {
        cl->static_vals = calloc(img->nstat_slots, sizeof(jslot));
        if (!cl->static_vals) {
            free(cl);
            return NULL;
        }
    }
    return cl;
}

int cp_ref(const jclass *cl, uint16_t idx, const char **cls, const char **name,
           const char **desc)
{
    const cp_entry *e;
    uint16_t nat;
    if (idx <= 0 || idx >= cl->cp_count)
        return FX_ERR_FORMAT;
    e = &cl->cp[idx];
    if (e->tag != CP_FIELDREF && e->tag != CP_METHODREF &&
        e->tag != CP_IFMETHODREF)
        return FX_ERR_FORMAT;
    if (cp_class_name(cl, (uint16_t)(e->v >> 16), cls) != FX_OK)
        return FX_ERR_FORMAT;
    nat = (uint16_t)(e->v & 0xffffu);
    if (nat <= 0 || nat >= cl->cp_count || cl->cp[nat].tag != CP_NAMEANDTYPE)
        return FX_ERR_FORMAT;
    if (cp_utf8_str(cl, (uint16_t)(cl->cp[nat].v >> 16), name) != FX_OK)
        return FX_ERR_FORMAT;
    return cp_utf8_str(cl, (uint16_t)(cl->cp[nat].v & 0xffffu), desc);
}
