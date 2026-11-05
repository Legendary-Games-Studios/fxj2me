/* fxj2me: prelink the boot class library into a const C ROM image.
 *
 * The runtime would otherwise parse each boot class out of a flash jar on
 * first use and keep its constant pool, strings, method tables and bytecode
 * in RAM — most of what a MIDlet launch has to fit. The library is fixed at
 * build time, so this tool parses it exactly the way the runtime does (same
 * parser, same linker) and writes the finished metadata as const C tables
 * that the linker places in flash. At run time a boot class then costs one
 * jclass header plus its static storage.
 *
 * usage: gen_bootimg <boot.jar> <out.c>
 */
#include "classfile.h"
#include "jar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CLASSES 512

static jclass *classes[MAX_CLASSES];
static int nclasses;

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *buf;

    if (!f) {
        fprintf(stderr, "gen_bootimg: cannot open %s\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = malloc((size_t)n ? (size_t)n : 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

/* Two-phase load: every class is parsed first so the linker resolves supers
 * by name exactly like the runtime does. */
static jclass *resolve(const char *name, void *ud)
{
    int i;
    (void)ud;
    for (i = 0; i < nclasses; i++)
        if (strcmp(classes[i]->name, name) == 0)
            return classes[i];
    fprintf(stderr, "gen_bootimg: missing super class %s\n", name);
    return NULL;
}

static int cmp_class(const void *a, const void *b)
{
    const jclass *x = *(jclass *const *)a;
    const jclass *y = *(jclass *const *)b;
    return strcmp(x->name, y->name);
}

/* Emit bytes as an octal-escaped string literal: compact, unambiguous, and
 * safe for any byte value (three digits always consume exactly one byte). */
static void put_data(FILE *f, const char *ctype, const char *tag, int idx,
                     const void *data, size_t len)
{
    const uint8_t *p = data;
    size_t i;

    fprintf(f, "static const %s %s%d[] =\n    \"", ctype, tag, idx);
    for (i = 0; i < len; i++) {
        if (i && i % 24 == 0)
            fprintf(f, "\"\n    \"");
        fprintf(f, "\\%03o", p[i]);
    }
    fprintf(f, "\";\n");
}

static void put_string(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\')
            fprintf(f, "\\%c", c);
        else if (c >= 0x20 && c < 0x7f && c != '?')
            fputc(c, f);
        else
            fprintf(f, "\\%03o", c);
    }
    fputc('"', f);
}

/* Longest string end in the class's blob (the block is NUL-separated). */
static size_t blob_len(const jclass *cl)
{
    size_t len = 0;
    int k;

    if (!cl->strblob)
        return 0;
    for (k = 0; k < cl->nstrs; k++) {
        size_t end = (size_t)(cl->strs[k] - cl->strblob) +
                     strlen(cl->strs[k]) + 1;
        if (end > len)
            len = end;
    }
    return len;
}

/*--- shared string table ------------------------------------------------*
 *
 * A MIDlet's classes repeat most of the boot library's pool text verbatim
 * ("(II)V", "Ljava/lang/String;", "javax/microedition/lcdui/Graphics", field
 * and method names). Emitting one hash table over every boot string lets the
 * class parser point the app's own pool at flash instead of copying, which is
 * most of the app's string memory on the real thing.
 */
#define STR_SLOTS 1024

static const char *strtab[8192];
static int strtab_cls[8192];
static long strtab_off[8192];
static int nstrtab;

static uint32_t str_hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

static void add_str(const char *s, int ci, long off)
{
    int i;
    for (i = 0; i < nstrtab; i++)
        if (strcmp(strtab[i], s) == 0)
            return;
    if (nstrtab < (int)(sizeof strtab / sizeof strtab[0])) {
        strtab[nstrtab] = s;
        strtab_cls[nstrtab] = ci;
        strtab_off[nstrtab] = off;
        nstrtab++;
    }
}

static void emit_strtab(FILE *out)
{
    uint16_t slot[STR_SLOTS];
    int i;

    for (i = 0; i < STR_SLOTS; i++)
        slot[i] = 0xffffu;
    for (i = 0; i < nstrtab; i++) {
        uint32_t h = str_hash(strtab[i]) & (STR_SLOTS - 1);
        while (slot[h] != 0xffffu)
            h = (h + 1) & (STR_SLOTS - 1);
        slot[h] = (uint16_t)i;
    }
    fprintf(out, "const char *const fx_rom_strs[] = {\n");
    for (i = 0; i < nstrtab; i++)
        fprintf(out, "    blob%d + %ld,\n", strtab_cls[i], strtab_off[i]);
    fprintf(out, "};\nconst unsigned fx_rom_nstrs = %d;\n\n", nstrtab);
    fprintf(out, "const uint16_t fx_rom_str_hash[%d] = {\n   ", STR_SLOTS);
    for (i = 0; i < STR_SLOTS; i++) {
        fprintf(out, "0x%04x,", slot[i]);
        if ((i & 15) == 15)
            fprintf(out, "\n   ");
    }
    fprintf(out, "\n};\n\n");
}

int main(int argc, char **argv)
{
    jar j;
    uint8_t *raw;
    size_t rawlen, strbytes = 0, strcount = 0, cpents = 0, methcount = 0;
    size_t codebytes = 0, shared = 0;
    int i, k;
    FILE *out;

    if (argc != 3) {
        fprintf(stderr, "usage: gen_bootimg <boot.jar> <out.c>\n");
        return 2;
    }
    raw = slurp(argv[1], &rawlen);
    if (!raw)
        return 1;
    if (jar_open(&j, raw, rawlen) != FX_OK) {
        fprintf(stderr, "gen_bootimg: not a jar: %s\n", argv[1]);
        free(raw);
        return 1;
    }

    for (i = 0; i < j.nentries; i++) {
        const char *ename = j.entries[i].name;
        size_t elen = strlen(ename), got = 0;
        uint8_t *ebuf;
        jclass *cl;

        if (elen < 7 || strcmp(ename + elen - 6, ".class") != 0)
            continue;                    /* manifest, resources */
        if (nclasses >= MAX_CLASSES) {
            fprintf(stderr, "gen_bootimg: too many classes\n");
            return 1;
        }
        ebuf = malloc(j.entries[i].usize ? j.entries[i].usize : 1);
        if (!ebuf) {
            fprintf(stderr, "gen_bootimg: out of memory\n");
            return 1;
        }
        if (jar_extract(&j, i, ebuf, j.entries[i].usize, &got) != FX_OK) {
            fprintf(stderr, "gen_bootimg: cannot extract %s\n", ename);
            free(ebuf);
            return 1;
        }
        if (class_parse(ebuf, got, &cl) != FX_OK) {
            fprintf(stderr, "gen_bootimg: cannot parse %s\n", ename);
            return 1;
        }
        classes[nclasses++] = cl;
    }
    qsort(classes, (size_t)nclasses, sizeof classes[0], cmp_class);

    for (i = 0; i < nclasses; i++) {
        if (class_link(classes[i], resolve, NULL) != FX_OK) {
            fprintf(stderr, "gen_bootimg: cannot link %s\n", classes[i]->name);
            return 1;
        }
        strbytes += blob_len(classes[i]);
        strcount += (size_t)classes[i]->nstrs;
        for (k = 0; k < classes[i]->nstrs; k++)
            if (classes[i]->strblob) {
                size_t before = (size_t)nstrtab;
                add_str(classes[i]->strs[k], i,
                        (long)(classes[i]->strs[k] - classes[i]->strblob));
                if ((size_t)nstrtab == before)
                    shared++;
            }
        cpents += (size_t)classes[i]->cp_count;
        methcount += (size_t)classes[i]->nmethods;
        codebytes += classes[i]->codes_len;
    }

    out = fopen(argv[2], "w");
    if (!out) {
        fprintf(stderr, "gen_bootimg: cannot write %s\n", argv[2]);
        return 1;
    }

    fprintf(out, "/* generated by tools/gen_bootimg.c from %s - do not edit */\n",
            argv[1]);
    fprintf(out, "#include \"romclass.h\"\n\n");

    for (i = 0; i < nclasses; i++) {
        const jclass *cl = classes[i];

        if (cl->strblob)
            put_data(out, "char", "blob", i, cl->strblob, blob_len(cl));
        if (cl->nstrs) {
            fprintf(out, "static const char *const strs%d[] = {", i);
            for (k = 0; k < cl->nstrs; k++)
                fprintf(out, " blob%d + %ld,", i,
                        (long)(cl->strs[k] - cl->strblob));
            fprintf(out, " };\n");
        }
        fprintf(out, "static const cp_entry cp%d[] = {\n", i);
        for (k = 0; k < cl->cp_count; k++)
            fprintf(out, "    {%u, %u, %u, %luu},\n", cl->cp[k].tag,
                    cl->cp[k].a, cl->cp[k].b, (unsigned long)cl->cp[k].v);
        fprintf(out, "};\n");

        if (cl->codes_len)
            put_data(out, "unsigned char", "codes", i, cl->codes,
                     cl->codes_len);
        if (cl->nmethods) {
            fprintf(out, "static const jmethod methods%d[] = {\n", i);
            for (k = 0; k < cl->nmethods; k++) {
                const jmethod *m = &cl->methods[k];
                fprintf(out, "    {%u, blob%d + %ld, blob%d + %ld, %u, %u, "
                             "%luu, ", m->access, i,
                        (long)(m->name - cl->strblob), i,
                        (long)(m->desc - cl->strblob), m->max_stack,
                        m->max_locals, (unsigned long)m->code_len);
                if (m->code_len)
                    fprintf(out, "codes%d + %ld, ", i,
                            (long)(m->code - cl->codes));
                else
                    fprintf(out, "0, ");
                fprintf(out, "%luu, ", (unsigned long)m->narg_slots);
                if (m->exc_len)
                    fprintf(out, "codes%d + %ld, %u},\n", i,
                            (long)(m->exc_table - cl->codes), m->exc_len);
                else
                    fprintf(out, "0, 0},\n");
            }
            fprintf(out, "};\n");
        }
        if (cl->nfields) {
            fprintf(out, "static const jfield fields%d[] = {\n", i);
            for (k = 0; k < cl->nfields; k++) {
                const jfield *fl = &cl->fields[k];
                fprintf(out, "    {%u, blob%d + %ld, blob%d + %ld, %luu, %d},\n",
                        fl->access, i, (long)(fl->name - cl->strblob), i,
                        (long)(fl->desc - cl->strblob),
                        (unsigned long)fl->slot, fl->is_ref ? 1 : 0);
            }
            fprintf(out, "};\n");
        }
        if (cl->nstatics) {
            fprintf(out, "static const jfield statics%d[] = {\n", i);
            for (k = 0; k < cl->nstatics; k++) {
                const jfield *fl = &cl->statics[k];
                fprintf(out, "    {%u, blob%d + %ld, blob%d + %ld, %luu, %d},\n",
                        fl->access, i, (long)(fl->name - cl->strblob), i,
                        (long)(fl->desc - cl->strblob),
                        (unsigned long)fl->slot, fl->is_ref ? 1 : 0);
            }
            fprintf(out, "};\n");
        }
        if (cl->nslots) {
            int nw = (int)((cl->nslots + 31) / 32);
            fprintf(out, "static const uint32_t refmask%d[] = {", i);
            for (k = 0; k < nw; k++)
                fprintf(out, " 0x%08lxu,", (unsigned long)cl->refmask[k]);
            fprintf(out, " };\n");
        }
        if (cl->nstat_slots) {
            int nw = (int)((cl->nstat_slots + 31) / 32);
            fprintf(out, "static const uint32_t srefmask%d[] = {", i);
            for (k = 0; k < nw; k++)
                fprintf(out, " 0x%08lxu,", (unsigned long)cl->srefmask[k]);
            fprintf(out, " };\n");
        }
        fprintf(out, "\n");
    }

    emit_strtab(out);

    fprintf(out, "const fx_rom_class fx_rom_classes[] = {\n");
    for (i = 0; i < nclasses; i++) {
        const jclass *cl = classes[i];
        fprintf(out, "    {.name = ");
        put_string(out, cl->name);
        if (cl->super_name) {
            fprintf(out, ", .super_name = ");
            put_string(out, cl->super_name);
        }
        fprintf(out, ", .access = %u, .cp_count = %u, .cp = cp%d,", cl->access,
                (unsigned)cl->cp_count, i);
        fprintf(out, " .strblob = blob%d, .nstrs = %d,", i, cl->nstrs);
        if (cl->nstrs)
            fprintf(out, " .strs = strs%d,", i);
        if (cl->nmethods)
            fprintf(out, " .methods = methods%d, .nmethods = %d,", i,
                    cl->nmethods);
        if (cl->nfields)
            fprintf(out, " .fields = fields%d, .nfields = %d,", i, cl->nfields);
        fprintf(out, " .nslots = %luu,", (unsigned long)cl->nslots);
        if (cl->nslots)
            fprintf(out, " .refmask = refmask%d,", i);
        if (cl->nstatics)
            fprintf(out, " .statics = statics%d, .nstatics = %d,", i,
                    cl->nstatics);
        fprintf(out, " .nstat_slots = %luu", (unsigned long)cl->nstat_slots);
        if (cl->nstat_slots)
            fprintf(out, ", .srefmask = srefmask%d", i);
        fprintf(out, " },\n");
    }
    fprintf(out, "};\n\nconst int fx_rom_nclasses = %d;\n", nclasses);

    fclose(out);
    fprintf(stderr,
            "boot image: %d classes, %lu cp entries, %lu methods, %lu strings "
            "(%lu bytes, %lu shared), %lu code bytes; %d hashable strings\n",
            nclasses, (unsigned long)cpents, (unsigned long)methcount,
            (unsigned long)strcount, (unsigned long)strbytes,
            (unsigned long)shared, (unsigned long)codebytes, nstrtab);
    return 0;
}
