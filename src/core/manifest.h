/* fxj2me: MANIFEST.MF / .jad parsing — MIDlet descriptors.
 *
 * A MIDlet suite is one jar plus an optional application descriptor (.jad).
 * The spec (MIDP 2.0, javax.microedition.midlet) requires the manifest main
 * section to carry MIDlet-Name, MIDlet-Version and MIDlet-Vendor, and requires
 * MIDlet-<n> entries (consecutive from 1, first gap ends the list) either in
 * the manifest or in the descriptor. Descriptor attributes take precedence
 * over manifest attributes of the same name.
 *
 * `midlet_info` holds the merged view; `midlet_select()` picks one MIDlet of
 * the suite for launching.
 */
#ifndef FXJ2ME_MANIFEST_H
#define FXJ2ME_MANIFEST_H

#include "jar.h"

#define MF_NAME_MAX   32
#define MF_CLASS_MAX  96
#define MF_ATTR_MAX   1024
#define MF_MAX_MIDLETS 8

typedef struct midlet_desc {
    char name[MF_NAME_MAX];        /* "MIDlet-<n>" display name */
    char icon[MF_NAME_MAX];        /* icon path ("" if none)    */
    char cls[MF_CLASS_MAX];        /* '/'-separated class name  */
} midlet_desc;

typedef struct midlet_info {
    /* MIDlet-1 (kept for callers that launch the single/first MIDlet) */
    char name[MF_NAME_MAX];
    char icon[MF_NAME_MAX];
    char cls[MF_CLASS_MAX];

    char midlet_name[MF_NAME_MAX]; /* MIDlet-Name (suite identity) */
    char vendor[MF_NAME_MAX];      /* MIDlet-Vendor                */
    char version[16];              /* MIDlet-Version               */

    /* Every MIDlet of the suite, in ordinal order (consecutive). */
    midlet_desc midlets[MF_MAX_MIDLETS];
    int nmidlets;

    uint32_t jar_size;             /* MIDlet-Jar-Size, 0 if absent  */
    uint32_t data_size;            /* MIDlet-Data-Size, 0 if absent */

    /* Main-section attributes packed as "key=value\0" strings in order;
     * `nattrs` entries, buffer overflow drops the rest. */
    char attrs[MF_ATTR_MAX];
    int nattrs;
} midlet_info;

/* Parse META-INF/MANIFEST.MF out of a jar. FX_OK when the attributes could be
 * parsed; the caller decides whether the suite is installable (MIDlet count,
 * required attributes). */
int manifest_parse(const jar *j, midlet_info *out);

/* Parse manifest/descriptor text directly (host tests). */
int manifest_parse_text(const char *text, size_t len, midlet_info *out);

/* Look up a stored attribute value (NULL pointer = missing). */
const char *manifest_attr(const midlet_info *info, const char *key);

/* True when `key` is present in the packed attribute list. */
int manifest_has(const midlet_info *info, const char *key);

/* Merge a descriptor over a manifest: `over` attributes win, attributes only
 * in `base` are kept. `out` is independent of both. */
void manifest_merge(const midlet_info *base, const midlet_info *over,
                    midlet_info *out);

/* Make MIDlet `idx` (0-based) the one described by name/icon/cls. Returns 0
 * on success, -1 if idx is out of range. */
int midlet_select(midlet_info *info, int idx);

/* True when the suite carries at least one MIDlet-<n> entry. */
int midlet_has_desc(const midlet_info *info);

#endif /* FXJ2ME_MANIFEST_H */
