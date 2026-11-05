/* fxj2me: native method registry. */
#ifndef FXJ2ME_NATIVES_H
#define FXJ2ME_NATIVES_H

#include "vm.h"

typedef int (*jnat_fn)(vm *v, jthread *t, jclass *oc,
                       const jslot *args, jslot *ret);

typedef struct jnat_entry {
    const char *cls;
    const char *name;
    const char *desc;
    jnat_fn fn;
} jnat_entry;

/* Register an additional native table (the MIDP layer registers its lcdui
 * natives this way). Registered tables are searched before the built-ins. */
void natives_add(const jnat_entry *tab, int count);

/* Dispatch a native call. `oc` is the resolved target class; the registry
 * walks its super chain for a matching (class, name, desc) entry. Returns
 * FX_OK, FX_ERR_UNIMPL (no such native), or FX_ERR_JAVA with t->exception
 * set (the interpreter should unwind). */
int natives_call(vm *v, jthread *t, jclass *oc, const char *name,
                 const char *desc, const jslot *args, jslot *ret);

#endif /* FXJ2ME_NATIVES_H */
