/* fxj2me: boot class library as a build-time ROM image.
 *
 * The boot library (lib/java) never changes at runtime, so parsing it out of
 * a flash jar and copying its constant pool, strings, method tables and
 * bytecode into RAM is pure waste — that is the bulk of what a MIDlet launch
 * has to fit in. tools/gen_bootimg.c prelinks it at build time into the const
 * tables below, which the linker places in flash (.rodata) with the add-in.
 *
 * Loading such a class then costs one jclass header plus its static storage;
 * everything else is read in place from flash.
 */
#ifndef FXJ2ME_ROMCLASS_H
#define FXJ2ME_ROMCLASS_H

#include "classfile.h"

typedef struct fx_rom_class {
    const char *name;
    const char *super_name;      /* NULL for java/lang/Object */
    uint16_t access;
    uint16_t cp_count;
    const cp_entry *cp;
    const char *strblob;         /* all pool strings, NUL-separated */
    const char *const *strs;     /* nstrs pointers into strblob */
    int nstrs;
    const jmethod *methods;
    int nmethods;
    const jfield *fields;        /* instance fields (slots precomputed) */
    int nfields;
    uint32_t nslots;             /* incl. inherited */
    const uint32_t *refmask;
    const jfield *statics;
    int nstatics;
    uint32_t nstat_slots;
    const uint32_t *srefmask;
} fx_rom_class;

extern const fx_rom_class fx_rom_classes[];
extern const int fx_rom_nclasses;

/* Every pool string in the image, with a flash-resident open-addressed hash
 * over it. A MIDlet's classes repeat most of this text (descriptors, class and
 * member names), so their parser stores a pointer into flash instead of
 * copying it. */
#define FX_ROM_STR_SLOTS 1024

extern const char *const fx_rom_strs[];
extern const uint16_t fx_rom_str_hash[];   /* index into fx_rom_strs, 0xffff = empty */
extern const unsigned fx_rom_nstrs;

/* The image's canonical copy of `s`, or NULL. */
const char *rom_intern(const char *s);

/* Image index of a boot class by binary name ("java/lang/Object"), or -1. */
int rom_find(const char *name);

/* Build a RAM jclass whose metadata points into the flash image. The only
 * allocation besides the header is the class's static storage. NULL when out
 * of memory. */
jclass *class_from_rom(int idx);

#endif /* FXJ2ME_ROMCLASS_H */
