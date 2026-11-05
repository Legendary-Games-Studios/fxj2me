/* fxj2me: bytecode interpreter VM. */
#ifndef FXJ2ME_VM_H
#define FXJ2ME_VM_H

#include "classfile.h"
#include "jar.h"
#include "pkg.h"

#define VM_MAX_CLASSES  128
#define VM_STACK_SLOTS  256
#define VM_LOCALS_SLOTS 256
#define VM_MAX_FRAMES   48
#define VM_MAX_INTERN   64      /* interned string literals            */
#define VM_ERRBUF       160

typedef struct jframe {
    jmethod *m;
    jclass *cls;            /* class declaring m (owns its cp/offsets)   */
    const uint8_t *pc;
    const uint8_t *caller_ipc;  /* caller's instruction start (unwind) */
    jslot *locals;
    int sp_base;            /* operand-stack base index in the thread stack */
} jframe;

typedef struct jthread {
    jslot stack[VM_STACK_SLOTS];
    uint8_t ttypes[VM_STACK_SLOTS];   /* 0 = primitive, 1 = reference */
    jslot locals[VM_LOCALS_SLOTS];
    uint8_t ltypes[VM_LOCALS_SLOTS];
    int ltop;
    jframe frames[VM_MAX_FRAMES];
    int nframes;
    struct vm *vm;
    const char *error;      /* human-readable fault, if status != FX_OK */
    jref exception;         /* pending/thrown Java exception object     */
} jthread;

typedef struct vm {
    jheap heap;
    jclass *classes[VM_MAX_CLASSES];
    int nclasses;
    jar *boot;              /* class library jar (java.*, javax.*), or NULL */
    jar *app;               /* the MIDlet jar (in-memory), or NULL          */
    char app_dir[96];       /* unpacked class directory (storage), "" = none */
    jthread *thread;

    jref interns[VM_MAX_INTERN];   /* string-literal intern table (roots) */
    int ninterns;
    jref tmp[2];            /* scratch GC roots for construction paths     */

    /* manifest attributes (MIDlet.getAppProperty), NUL-separated text */
    const char *attrs;
    int nattrs;

    char errbuf[VM_ERRBUF]; /* formatted fault text backing thread->error  */
} vm;

/* boot may be NULL (core-level tests without a class library). */
int vm_init(vm *v, void *heap_block, size_t heap_size, jar *boot, jar *app);

/* Free all classes and class metadata owned by the VM. The heap block, thread
 * and vm struct itself remain owned by the caller. */
void vm_free(vm *v);

/* Load (or fetch) a class by binary name ("java/lang/Object" style) and
 * link it. Returns NULL on failure. Search order: already-loaded, boot jar,
 * app jar. */
jclass *vm_load_class(vm *v, const char *name);

/* Run a static method: args in `args` (in declaration order, wide values in
 * two slots), result in *ret (two slots for wide results). */
int vm_call_static(vm *v, jthread *t, const char *cls, const char *method,
                   const char *desc, const jslot *args, jslot *ret);

/* Run any method with an explicit receiver (JREF_NULL for static). */
int vm_call(vm *v, jthread *t, jclass *cls, const char *method,
            const char *desc, jref receiver, const jslot *args, jslot *ret);

/* Raw heap allocation with zero-fill + GC retry (JREF_NULL on failure).
 * Used by natives that build objects outside the interpreter. */
jref vm_alloc(vm *v, size_t bytes, jclass *cls, uint32_t flags, uint32_t aux);

/* java/lang/Class mirror object for a class (cached in the jclass). */
jref vm_class_mirror(vm *v, jclass *c);

/*--- strings ----------------------------------------------------------*/

/* Create a java/lang/String from modified-UTF-8 class-file bytes. */
jref vm_make_string(vm *v, const uint8_t *mutf8, int len);

/* Create a java/lang/String from UTF-16 code units. */
jref vm_make_string16(vm *v, const uint16_t *chars, int len);

/* Create a java/lang/String from a NUL-terminated ASCII/Latin-1 C string. */
jref vm_make_cstring(vm *v, const char *s);

/* Access a String's characters (reads its value/count fields).
 * Returns FX_OK and sets *chars and *len; *chars is empty for NULL. */
int vm_str_chars(vm *v, jref s, const uint16_t **chars, int *len);

/* Content equality against UTF-16 chars (NULL never equals). */
bool vm_str_equals16(vm *v, jref s, const uint16_t *chars, int len);

/* Interned lookup: returns an existing interned equal string or JREF_NULL. */
jref vm_intern_find(vm *v, const uint16_t *chars, int len);

/*--- exceptions -------------------------------------------------------*/

/* Allocate an exception object of the named class with an optional message.
 * Returns JREF_NULL on failure. The class's <init> is not run; the message
 * field is set directly (exception classes are simple value holders). */
jref vm_make_exception(vm *v, jthread *t, const char *cls, const char *msg);

/* Raise a Java exception by class name ("java/lang/NullPointerException"),
 * with optional message. Sets t->exception and returns FX_ERR_JAVA. */
int vm_throw(vm *v, jthread *t, const char *cls, const char *msg);

/* Format a pending/uncaught exception as "java.lang.Foo: msg" into buf. */
void vm_exc_text(vm *v, jthread *t, char *buf, size_t cap);

#endif /* FXJ2ME_VM_H */
