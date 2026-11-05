/* fxj2me: .class file parsing into runtime metadata.
 *
 * Class bytes are kept resident (they back the Code attributes); on target
 * they live in the class cache backed by flash/jar.
 */
#ifndef FXJ2ME_CLASSFILE_H
#define FXJ2ME_CLASSFILE_H

#include "fxj2me.h"
#include "mem.h"

enum {
    CP_UTF8 = 1, CP_INTEGER = 3, CP_FLOAT = 4, CP_LONG = 5, CP_DOUBLE = 6,
    CP_CLASS = 7, CP_STRING = 8, CP_FIELDREF = 9, CP_METHODREF = 10,
    CP_IFMETHODREF = 11, CP_NAMEANDTYPE = 12, CP_METHODHANDLE = 15,
    CP_METHODTYPE = 16, CP_DYNAMIC = 17, CP_INVOKEDYNAMIC = 18
};

enum {
    ACC_PUBLIC = 0x0001, ACC_PRIVATE = 0x0002, ACC_PROTECTED = 0x0004,
    ACC_STATIC = 0x0008, ACC_FINAL = 0x0010, ACC_SUPER = 0x0020,
    ACC_NATIVE = 0x0100, ACC_INTERFACE = 0x0200, ACC_ABSTRACT = 0x0400
};

/* Packed constant-pool entry: 8 bytes on both host (64-bit) and target
 * (32-bit). A pointer-sized union made this 16-24 bytes and cost more RAM
 * than the class's bytecode, so every tag stores indices instead:
 *
 *   UTF8                  v = index into jclass.strs, a/b = length
 *   INTEGER, FLOAT        v = bit pattern
 *   LONG, DOUBLE          v = low 32 bits, next slot's v = high 32 bits
 *   CLASS, STRING, MTYPE  v = pool index
 *   refs, NAMEANDTYPE     v = (first index << 16) | second index
 *   METHODHANDLE          v = pool index
 */
typedef struct cp_entry {
    uint8_t tag;
    uint8_t a, b;      /* 16-bit auxiliary (UTF-8 byte length) */
    uint32_t v;
} cp_entry;

typedef struct jfield {
    uint16_t access;
    const char *name;
    const char *desc;
    uint32_t slot;        /* slot index (wide fields take two) */
    bool is_ref;          /* declared type is a reference */
} jfield;

typedef struct jmethod {
    uint16_t access;
    const char *name;
    const char *desc;
    uint16_t max_stack, max_locals;
    uint32_t code_len;
    const uint8_t *code;  /* into the class byte buffer */
    uint32_t narg_slots;  /* argument slots incl. receiver */
    /* exception table (Code attribute) */
    const uint8_t *exc_table;
    uint16_t exc_len;     /* number of 8-byte entries */
} jmethod;

typedef struct jclass {
    char *name;
    char *super_name;
    struct jclass *super;
    uint16_t access;

    cp_entry *cp;
    int cp_count;

    /* Class-file strings (UTF-8 pool entries), one copy each; the pool keeps
     * indices into this table. After parsing they are packed into strblob,
     * so one allocation (and one block header) covers them all. */
    char **strs;
    int nstrs, strcap;
    char *strblob;

    jmethod *methods;
    int nmethods;

    jfield *fields;       /* instance fields declared in this class */
    int nfields;
    uint32_t nslots;      /* total instance slots incl. inherited */
    uint32_t *refmask;    /* bit i set => instance slot i is a jref */

    jfield *statics;
    int nstatics;
    uint32_t nstat_slots;
    uint32_t *srefmask;
    jslot *static_vals;   /* static storage (nstat_slots) */

    /* Bytecode only: every method's Code bytes and exception table packed
     * into one buffer after parsing. The original class file is dropped, so
     * the constant-pool text and attribute headers in it cost no RAM. */
    uint8_t *codes;
    uint32_t codes_len;
    uint8_t *bytes;       /* raw class file, freed once codes are extracted */
    size_t nbytes;

    jref mirror;          /* java/lang/Class instance, lazily created */

    bool linked;
    bool initialized;
    bool rom;             /* metadata points into the boot flash image:
                           * nothing above is owned by this class */
} jclass;

/* Parse class bytes (takes ownership of `bytes` via free() on failure or
 * class_free()). */
int class_parse(uint8_t *bytes, size_t len, jclass **out);
void class_free(jclass *c);

/* Resolve super classes and compute field slots/refmasks. `resolve` returns
 * the named class (loading it if needed) or NULL. */
int class_link(jclass *c, jclass *(*resolve)(const char *name, void *ud),
               void *ud);

/* Method/field lookup walking the super chain. The _decl variant also reports
 * the class that declares the method (its constant pool owns the method's
 * offsets); class_find_method drops it. */
jmethod *class_find_method(jclass *c, const char *name, const char *desc);
jmethod *class_find_method_decl(jclass *c, const char *name, const char *desc,
                                jclass **decl);
int class_find_field(jclass *c, const char *name, const char *desc,
                     bool is_static, uint32_t *slot, bool *is_ref);

/* Literal constant-pool values (long/double span two slots). */
int32_t cp_ival(const cp_entry *e);
float cp_fval(const cp_entry *e);
int64_t cp_lval(const jclass *c, int idx);
double cp_dval(const jclass *c, int idx);

/* Constant-pool helpers. */
int cp_class_name(const jclass *c, uint16_t idx, const char **name);
int cp_string_utf8(const jclass *c, uint16_t idx, const uint8_t **p,
                   uint16_t *len);
int cp_ref(const jclass *c, uint16_t idx, const char **cls, const char **name,
           const char **desc);

/* Argument slot count from a method descriptor (incl. receiver). */
uint32_t desc_arg_slots(const char *desc, bool is_static);

#endif /* FXJ2ME_CLASSFILE_H */
