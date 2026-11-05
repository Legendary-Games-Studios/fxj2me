/* fxj2me: common types for the VM core.
 *
 * Portability rules:
 *  - jref is a 32-bit offset into the VM heap (0 = null), identical on the
 *    64-bit host and 32-bit SH-4A target.
 *  - Every JVM value occupies one 32-bit slot; longs/doubles occupy two.
 */
#ifndef FXJ2ME_H
#define FXJ2ME_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint32_t jref;
#define JREF_NULL ((jref)0)

/* One JVM stack/local slot. */
typedef union jslot {
    int32_t i;
    float   f;
    jref    r;
} jslot;

/* Long/double helpers (two-slot access, little-endian word order like JVM). */
static inline int64_t jslot_get_long(const jslot *lo)
{
    return (int64_t)(uint32_t)lo[0].i | ((int64_t)(uint32_t)lo[1].i << 32);
}

static inline void jslot_set_long(jslot *lo, int64_t v)
{
    lo[0].i = (int32_t)(uint32_t)v;
    lo[1].i = (int32_t)(uint32_t)(v >> 32);
}

static inline double jslot_get_double(const jslot *lo)
{
    int64_t bits = jslot_get_long(lo);
    double d;
    __builtin_memcpy(&d, &bits, sizeof d);
    return d;
}

static inline void jslot_set_double(jslot *lo, double d)
{
    int64_t bits;
    __builtin_memcpy(&bits, &d, sizeof bits);
    jslot_set_long(lo, bits);
}

/* Status codes: 0 = ok, negative = failure. */
enum {
    FX_OK          = 0,
    FX_ERR         = -1,   /* generic failure          */
    FX_ERR_FORMAT  = -2,   /* malformed input          */
    FX_ERR_NOSPACE = -3,   /* out of memory / capacity */
    FX_ERR_NOTFOUND = -4,  /* missing symbol / entry   */
    FX_ERR_UNIMPL  = -5,   /* not implemented (yet)    */
    FX_ERR_JAVA    = -6,   /* Java-level exception     */
};

#endif /* FXJ2ME_H */
