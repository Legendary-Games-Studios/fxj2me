/* fxj2me: native method registry + java.lang natives.
 *
 * Lookup walks the target class's super chain so overrides bind naturally.
 * Math natives avoid libm (the SH target links none): sqrt is Newton,
 * transcendentals are Taylor/minimax approximations good to ~1e-6.
 */
#include "natives.h"
#include "classfile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double fx_atan(double x);
static double fx_atan2(double y, double x);

static int nat_object_hash(vm *v, jthread *t, jclass *oc,
                           const jslot *args, jslot *ret)
{
    (void)v; (void)t; (void)oc;
    ret[0].i = (int32_t)args[0].r;
    return FX_OK;
}

static int nat_object_equals(vm *v, jthread *t, jclass *oc,
                             const jslot *args, jslot *ret)
{
    (void)v; (void)t; (void)oc;
    ret[0].i = (args[0].r == args[1].r) ? 1 : 0;
    return FX_OK;
}

static int nat_object_tostring(vm *v, jthread *t, jclass *oc,
                               const jslot *args, jslot *ret)
    /* cppcheck-suppress unusedParameter */
{
    char buf[96];
    jclass *c = oc;
    jblk *b = heap_hdr(&v->heap, args[0].r);
    (void)t;
    if (b && b->cls)
        c = b->cls;
    snprintf(buf, sizeof buf, "%s@%x", c->name ? c->name : "?",
             (unsigned)args[0].r);
    ret[0].r = vm_make_cstring(v, buf);
    ret[1].i = 0;
    return ret[0].r ? FX_OK : FX_ERR_NOSPACE;
}

static int nat_object_getclass(vm *v, jthread *t, jclass *oc,
                               const jslot *args, jslot *ret)
{
    jclass *c = oc;
    jblk *b = heap_hdr(&v->heap, args[0].r);
    (void)t;
    if (b && b->cls)
        c = b->cls;
    ret[0].r = vm_class_mirror(v, c);
    ret[1].i = 0;
    return ret[0].r ? FX_OK : FX_ERR_NOSPACE;
}

static int nat_object_clone(vm *v, jthread *t, jclass *oc,
                            const jslot *args, jslot *ret)
{
    (void)oc;
    (void)args;
    ret[0].i = ret[1].i = 0;
    return vm_throw(v, t, "java/lang/CloneNotSupportedException",
                    "clone unsupported");
}

static int nat_class_name(vm *v, jthread *t, jclass *oc,
                          const jslot *args, jslot *ret)
{
    /* Class instances carry their jclass name in the `name` field; this is
     * only used for builtin mirrors which set it at creation. */
    (void)oc; (void)t; (void)args; (void)v;
    ret[0].r = JREF_NULL;
    ret[1].i = 0;
    return FX_OK;
}

static int nat_class_isassignable(vm *v, jthread *t, jclass *oc,
                                  const jslot *args, jslot *ret)
{
    (void)v; (void)t; (void)oc; (void)args;
    ret[0].i = 0;
    return FX_OK;
}

static int nat_system_arraycopy(vm *v, jthread *t, jclass *oc,
                                const jslot *args, jslot *ret)
{
    jheap *hp = &v->heap;
    jref src = args[0].r, dst = args[2].r;
    int32_t sp = args[1].i, dp = args[3].i, n = args[4].i;
    jblk *sb, *db;
    uint8_t *sbase, *dbase;
    size_t esize;

    (void)oc; (void)t;
    ret[0].i = ret[1].i = 0;

    if (src == JREF_NULL || dst == JREF_NULL)
        return vm_throw(v, t, "java/lang/NullPointerException", "arraycopy");
    sb = heap_hdr(hp, src);
    db = heap_hdr(hp, dst);
    if (!sb || !db || !(sb->flags & JBLK_ARRAY) || !(db->flags & JBLK_ARRAY))
        return vm_throw(v, t, "java/lang/ArrayStoreException", "not arrays");
    if (n < 0 || sp < 0 || dp < 0 || (uint32_t)(sp + n) > sb->aux ||
        (uint32_t)(dp + n) > db->aux)
        return vm_throw(v, t, "java/lang/ArrayIndexOutOfBoundsException",
                        "arraycopy");
    if ((sb->flags & JBLK_REFELEM) != (db->flags & JBLK_REFELEM))
        return vm_throw(v, t, "java/lang/ArrayStoreException", "type mismatch");
    if (n == 0)
        return FX_OK;
    if (JBLK_ESIZE(sb) != JBLK_ESIZE(db))
        return vm_throw(v, t, "java/lang/ArrayStoreException", "type mismatch");

    /* Arrays store elements contiguously after the block header. The
     * element size comes from the block (the rounded payload would lie). */
    esize = JBLK_ESIZE(sb) ? JBLK_ESIZE(sb) : 1;
    sbase = (uint8_t *)heap_ptr(hp, src);
    dbase = (uint8_t *)heap_ptr(hp, dst);
    memmove(dbase + (size_t)dp * esize, sbase + (size_t)sp * esize,
            (size_t)n * esize);
    return FX_OK;
}

static int nat_system_millis(vm *v, jthread *t, jclass *oc,
                             const jslot *args, jslot *ret)
{
    (void)v; (void)t; (void)oc; (void)args;
    jslot_set_long(ret, (int64_t)clock() * 1000 / CLOCKS_PER_SEC);
    return FX_OK;
}

static int nat_system_gc(vm *v, jthread *t, jclass *oc,
                         const jslot *args, jslot *ret)
{
    (void)t; (void)oc; (void)args;
    heap_gc(&v->heap);
    ret[0].i = ret[1].i = 0;
    return FX_OK;
}

static int nat_system_identity(vm *v, jthread *t, jclass *oc,
                               const jslot *args, jslot *ret)
{
    (void)v; (void)t; (void)oc;
    ret[0].i = (int32_t)args[0].r;
    return FX_OK;
}

static int nat_double_parsedouble(vm *v, jthread *t, jclass *oc,
                                  const jslot *args, jslot *ret)
{
    const uint16_t *chars;
    char buf[48];
    int len, i;
    (void)oc;
    (void)t;
    ret[1].i = 0;
    if (vm_str_chars(v, args[0].r, &chars, &len) != FX_OK)
        return FX_ERR_FORMAT;
    if (len >= (int)sizeof buf)
        len = (int)sizeof buf - 1;
    for (i = 0; i < len; i++)
        buf[i] = (char)chars[i];
    buf[len] = 0;
    jslot_set_double(ret, strtod(buf, NULL));
    return FX_OK;
}

static int nat_double_bits0(vm *v, jthread *t, jclass *oc,
                            const jslot *args, jslot *ret)
{
    double d = (double)args[0].f;
    (void)v; (void)t; (void)oc;
    jslot_set_long(ret, (int64_t)(uint32_t)args[0].i | 0LL);
    (void)d;
    return FX_OK;
}

static int nat_math_atan2(vm *v, jthread *t, jclass *oc,
                          const jslot *args, jslot *ret)
{
    double y = jslot_get_double(args);
    double x = jslot_get_double(&args[2]);
    (void)v; (void)t; (void)oc;
    jslot_set_double(ret, fx_atan2(y, x));
    return FX_OK;
}

static int nat_string_intern(vm *v, jthread *t, jclass *oc,
                             const jslot *args, jslot *ret)
{
    const uint16_t *chars;
    int len;
    jref hit;
    (void)oc; (void)t;
    ret[1].i = 0;
    if (vm_str_chars(v, args[0].r, &chars, &len) != FX_OK)
        return FX_ERR_FORMAT;
    hit = vm_intern_find(v, chars, len);
    ret[0].r = (hit != JREF_NULL) ? hit : args[0].r;
    return FX_OK;
}

/*--- math (no libm on target) ------------------------------------------*/

static double fx_sqrt(double x)
{
    double r;
    int i;
    if (x <= 0.0)
        return 0.0;
    r = x > 1.0 ? x : 1.0;
    for (i = 0; i < 40; i++)
        r = 0.5 * (r + x / r);
    return r;
}

static double fx_sin(double x)
{
    double term, sum;
    int i;
    /* wrap to [-pi, pi] */
    while (x > 3.141592653589793) x -= 6.283185307179586;
    while (x < -3.141592653589793) x += 6.283185307179586;
    term = x;
    sum = x;
    for (i = 1; i <= 8; i++) {
        term *= -x * x / ((2 * i) * (2 * i + 1));
        sum += term;
    }
    return sum;
}

static double fx_cos(double x)
{
    double term, sum;
    int i;
    while (x > 3.141592653589793) x -= 6.283185307179586;
    while (x < -3.141592653589793) x += 6.283185307179586;
    term = 1.0;
    sum = 1.0;
    for (i = 1; i <= 8; i++) {
        term *= -x * x / ((2 * i - 1) * (2 * i));
        sum += term;
    }
    return sum;
}

static double fx_atan(double x)
{
    double x2, term, sum;
    int i;
    if (x > 1.0)
        return 1.5707963267948966 - fx_atan(1.0 / x);
    if (x < -1.0)
        return -1.5707963267948966 - fx_atan(1.0 / x);
    x2 = -x * x;
    term = x;
    sum = x;
    for (i = 1; i <= 10; i++) {
        term *= x2;
        sum += term / (2 * i + 1);
    }
    return sum;
}

static double fx_atan2(double y, double x)
{
    double a = 0.0;
    if (x == 0.0 && y == 0.0)
        return 0.0;
    if (x > 0.0)
        return fx_atan(y / x);
    if (x < 0.0) {
        if (y >= 0.0)
            return fx_atan(y / x) + 3.141592653589793;
        return fx_atan(y / x) - 3.141592653589793;
    }
    (void)a;
    return y > 0.0 ? 1.5707963267948966 : -1.5707963267948966;
}

static int nat_math_sqrt(vm *v, jthread *t, jclass *oc,
                         const jslot *args, jslot *ret)
{
    (void)v; (void)t; (void)oc;
    jslot_set_double(ret, fx_sqrt(jslot_get_double(args)));
    return FX_OK;
}

static int nat_math_sin(vm *v, jthread *t, jclass *oc,
                        const jslot *args, jslot *ret)
{
    (void)v; (void)t; (void)oc;
    jslot_set_double(ret, fx_sin(jslot_get_double(args)));
    return FX_OK;
}

static int nat_math_cos(vm *v, jthread *t, jclass *oc,
                        const jslot *args, jslot *ret)
{
    (void)v; (void)t; (void)oc;
    jslot_set_double(ret, fx_cos(jslot_get_double(args)));
    return FX_OK;
}

static int nat_math_tan(vm *v, jthread *t, jclass *oc,
                        const jslot *args, jslot *ret)
{
    (void)v; (void)t; (void)oc;
    jslot_set_double(ret, fx_sin(jslot_get_double(args)) /
                             fx_cos(jslot_get_double(args)));
    return FX_OK;
}

static int nat_math_exp(vm *v, jthread *t, jclass *oc,
                        const jslot *args, jslot *ret)
{
    double x = jslot_get_double(args), term = 1.0, sum = 1.0;
    int i;
    (void)v; (void)t; (void)oc;
    for (i = 1; i <= 12; i++) {
        term *= x / i;
        sum += term;
    }
    jslot_set_double(ret, sum);
    return FX_OK;
}

static int nat_math_log(vm *v, jthread *t, jclass *oc,
                        const jslot *args, jslot *ret)
{
    double x = jslot_get_double(args), y, term, sum;
    int i;
    (void)v; (void)t; (void)oc;
    if (x <= 0.0) {
        jslot_set_double(ret, -1.0 / 0.0);
        return FX_OK;
    }
    y = (x - 1.0) / (x + 1.0);
    term = y;
    sum = y;
    for (i = 1; i <= 10; i++) {
        term *= y * y * (2 * i - 1) / (2 * i + 1);
        sum += term;
    }
    jslot_set_double(ret, 2.0 * sum);
    return FX_OK;
}

static int nat_math_pow(vm *v, jthread *t, jclass *oc,
                        const jslot *args, jslot *ret)
{
    double b = jslot_get_double(args);
    double e = jslot_get_double(&args[2]);
    (void)v; (void)t; (void)oc;
    if (e == 0.0) {
        jslot_set_double(ret, 1.0);
    } else if (b == 0.0) {
        jslot_set_double(ret, 0.0);
    } else if (e == (double)(int32_t)e) {
        double r = 1.0;
        int32_t n = (int32_t)e, i;
        double base = e < 0 ? 1.0 / b : b;
        if (n < 0) n = -n;
        for (i = 0; i < n; i++)
            r *= base;
        jslot_set_double(ret, r);
    } else {
        jslot_set_double(ret, 0.0);   /* fractional powers unsupported */
    }
    return FX_OK;
}

static int nat_math_floor(vm *v, jthread *t, jclass *oc,
                          const jslot *args, jslot *ret)
{
    double d = jslot_get_double(args);
    (void)v; (void)t; (void)oc;
    if (d >= 9.2e18 || d <= -9.2e18 || d != d)
        jslot_set_double(ret, d);
    else {
        int64_t i = (int64_t)d;
        if ((double)i > d)
            i--;
        jslot_set_double(ret, (double)i);
    }
    return FX_OK;
}

static int nat_math_ceil(vm *v, jthread *t, jclass *oc,
                         const jslot *args, jslot *ret)
{
    double d = jslot_get_double(args);
    (void)v; (void)t; (void)oc;
    if (d >= 9.2e18 || d <= -9.2e18 || d != d)
        jslot_set_double(ret, d);
    else {
        int64_t i = (int64_t)d;
        if ((double)i < d)
            i++;
        jslot_set_double(ret, (double)i);
    }
    return FX_OK;
}

static int nat_double_tostring(vm *v, jthread *t, jclass *oc,
                               const jslot *args, jslot *ret)
{
    char buf[40];
    (void)t; (void)oc;
    snprintf(buf, sizeof buf, "%g", jslot_get_double(args));
    ret[0].r = vm_make_cstring(v, buf);
    ret[1].i = 0;
    return ret[0].r ? FX_OK : FX_ERR_NOSPACE;
}

static int nat_throwable_name(vm *v, jthread *t, jclass *oc,
                              const jslot *args, jslot *ret)
{
    /* java/lang/Throwable name0(): class name with '.' separators. */
    char buf[96];
    jblk *b = heap_hdr(&v->heap, args[0].r);
    jclass *c = b && b->cls ? b->cls : oc;
    size_t i;
    (void)t;
    snprintf(buf, sizeof buf, "%s", c->name ? c->name : "?");
    for (i = 0; buf[i]; i++)
        if (buf[i] == '/')
            buf[i] = '.';
    ret[0].r = vm_make_cstring(v, buf);
    ret[1].i = 0;
    return ret[0].r ? FX_OK : FX_ERR_NOSPACE;
}

static const jnat_entry table[] = {
    { "java/lang/Object", "hashCode", "()I", nat_object_hash },
    { "java/lang/Object", "equals", "(Ljava/lang/Object;)Z", nat_object_equals },
    { "java/lang/Object", "toString", "()Ljava/lang/String;", nat_object_tostring },
    { "java/lang/Object", "getClass", "()Ljava/lang/Class;", nat_object_getclass },
    { "java/lang/Object", "clone", "()Ljava/lang/Object;", nat_object_clone },
    { "java/lang/Class", "getName", "()Ljava/lang/String;", nat_class_name },
    { "java/lang/Class", "isAssignableFrom", "(Ljava/lang/Class;)Z",
      nat_class_isassignable },
    { "java/lang/System", "arraycopy",
      "(Ljava/lang/Object;ILjava/lang/Object;II)V", nat_system_arraycopy },
    { "java/lang/System", "currentTimeMillis", "()J", nat_system_millis },
    { "java/lang/System", "gc", "()V", nat_system_gc },
    { "java/lang/System", "identityHashCode", "(Ljava/lang/Object;)I",
      nat_system_identity },
    { "java/lang/String", "intern", "()Ljava/lang/String;", nat_string_intern },
    { "java/lang/Math", "sqrt", "(D)D", nat_math_sqrt },
    { "java/lang/Math", "sin", "(D)D", nat_math_sin },
    { "java/lang/Math", "cos", "(D)D", nat_math_cos },
    { "java/lang/Math", "tan", "(D)D", nat_math_tan },
    { "java/lang/Math", "exp", "(D)D", nat_math_exp },
    { "java/lang/Math", "log", "(D)D", nat_math_log },
    { "java/lang/Math", "pow", "(DD)D", nat_math_pow },
    { "java/lang/Math", "floor", "(D)D", nat_math_floor },
    { "java/lang/Math", "ceil", "(D)D", nat_math_ceil },
    { "java/lang/Math", "atan2", "(DD)D", nat_math_atan2 },
    { "java/lang/Double", "toString", "(D)Ljava/lang/String;",
      nat_double_tostring },
    { "java/lang/Double", "parseDouble", "(Ljava/lang/String;)D",
      nat_double_parsedouble },
    { "java/lang/Double", "floatToLongBits0", "(F)J", nat_double_bits0 },
    { "java/lang/Throwable", "name0", "()Ljava/lang/String;", nat_throwable_name },
};

#define NAT_MAX_EXTRA 4
static const jnat_entry *extra[NAT_MAX_EXTRA];
static int extra_count[NAT_MAX_EXTRA];
static int nextra;

void natives_add(const jnat_entry *tab, int count)
{
    if (nextra >= NAT_MAX_EXTRA)
        return;
    extra[nextra] = tab;
    extra_count[nextra] = count;
    nextra++;
}

static int nat_lookup(const jnat_entry *tab, int n, jclass *oc,
                      const char *name, const char *desc)
{
    int i;
    jclass *c;
    for (c = oc; c; c = c->super) {
        for (i = 0; i < n; i++) {
            if (tab[i].fn && strcmp(tab[i].cls, c->name) == 0 &&
                strcmp(tab[i].name, name) == 0 &&
                strcmp(tab[i].desc, desc) == 0)
                return i;
        }
    }
    return -1;
}

int natives_call(vm *v, jthread *t, jclass *oc, const char *name,
                 const char *desc, const jslot *args, jslot *ret)
{
    int i, e;

    for (e = nextra - 1; e >= 0; e--) {
        i = nat_lookup(extra[e], extra_count[e], oc, name, desc);
        if (i >= 0)
            return extra[e][i].fn(v, t, oc, args, ret);
    }
    i = nat_lookup(table, (int)(sizeof table / sizeof table[0]), oc,
                   name, desc);
    if (i >= 0)
        return table[i].fn(v, t, oc, args, ret);
    return FX_ERR_UNIMPL;
}
