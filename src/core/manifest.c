/* fxj2me: MANIFEST.MF / .jad parsing implementation. */
#include "manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_bounded(char *dst, size_t cap, const char *src, size_t len)
{
    if (cap == 0)
        return;
    if (len >= cap)
        len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

/* Trim ASCII spaces on both ends. */
static void trim(const char **p, size_t *n)
{
    while (*n > 0 && (**p == ' ' || **p == '\t')) {
        (*p)++;
        (*n)--;
    }
    while (*n > 0 && ((*p)[*n - 1] == ' ' || (*p)[*n - 1] == '\t'))
        (*n)--;
}

/* Bytes used by the packed "key=value\0" list. */
static size_t attrs_used(const midlet_info *info)
{
    size_t used = 0;
    int i;
    for (i = 0; i < info->nattrs; i++)
        used += strlen(info->attrs + used) + 1;
    return used;
}

/* Store one attribute in the packed "key=value\0" buffer. */
static void add_attr(midlet_info *out, const char *key, size_t klen,
                     const char *val, size_t vlen)
{
    size_t need = klen + 1 + vlen + 1;
    size_t used = attrs_used(out);
    char *p;

    if (used + need > sizeof out->attrs)
        return;                                     /* buffer full: drop */
    p = out->attrs + used;
    memcpy(p, key, klen);
    p[klen] = '=';
    memcpy(p + klen + 1, val, vlen);
    p[klen + 1 + vlen] = 0;
    out->nattrs++;
}

const char *manifest_attr(const midlet_info *info, const char *key)
{
    size_t used = 0, klen = strlen(key);
    int i;
    for (i = 0; i < info->nattrs; i++) {
        const char *e = info->attrs + used;
        if (strncmp(e, key, klen) == 0 && e[klen] == '=')
            return e + klen + 1;
        used += strlen(e) + 1;
    }
    return NULL;
}

int manifest_has(const midlet_info *info, const char *key)
{
    return manifest_attr(info, key) != NULL;
}

/* Parse an unsigned decimal attribute value. */
static uint32_t attr_u32(const char *s)
{
    uint32_t v = 0;
    int digits = 0;

    if (!s)
        return 0;
    while (*s == ' ' || *s == '\t')
        s++;
    while (*s >= '0' && *s <= '9' && digits < 10) {
        v = v * 10 + (uint32_t)(*s - '0');
        s++;
        digits++;
    }
    return digits ? v : 0;
}

/* "MIDlet-<n>: name, icon, class" -> midlet_desc. The icon may be empty but
 * the separator must still be present. */
static int parse_midlet_line(const char *val, size_t vlen, midlet_desc *d)
{
    const char *parts[3];
    size_t plen[3];
    int nparts = 0;
    size_t s = 0;
    int i;

    memset(d, 0, sizeof *d);
    for (i = 0; i < 3; i++) {
        size_t e = s;
        while (e < vlen && val[e] != ',')
            e++;
        parts[nparts] = val + s;
        plen[nparts] = e - s;
        trim(&parts[nparts], &plen[nparts]);
        nparts++;
        if (e >= vlen)
            break;
        s = e + 1;
    }
    if (nparts < 3 || plen[2] == 0)
        return -1;                 /* malformed entry: no class name */
    copy_bounded(d->name, sizeof d->name, parts[0], plen[0]);
    copy_bounded(d->icon, sizeof d->icon, parts[1], plen[1]);
    copy_bounded(d->cls, sizeof d->cls, parts[2], plen[2]);
    for (i = 0; d->cls[i]; i++)    /* dots -> slashes (binary name) */
        if (d->cls[i] == '.')
            d->cls[i] = '/';
    return 0;
}

/* Fill the MIDlet table from the packed attributes: MIDlet-1..N consecutive,
 * the first missing ordinal ends the list (MIDP 2.0). */
static void build_midlets(midlet_info *out)
{
    int i;

    out->nmidlets = 0;
    for (i = 1; i <= MF_MAX_MIDLETS; i++) {
        char key[16];
        const char *v;

        sprintf(key, "MIDlet-%d", i);
        v = manifest_attr(out, key);
        if (!v)
            break;
        if (parse_midlet_line(v, strlen(v), &out->midlets[i - 1]) != 0)
            break;
        out->nmidlets = i;
    }

    /* Legacy single-MIDlet fields mirror the first entry. */
    out->name[0] = out->icon[0] = out->cls[0] = 0;
    if (out->nmidlets > 0) {
        memcpy(out->name, out->midlets[0].name, sizeof out->name);
        memcpy(out->icon, out->midlets[0].icon, sizeof out->icon);
        memcpy(out->cls, out->midlets[0].cls, sizeof out->cls);
    }
}

int midlet_select(midlet_info *info, int idx)
{
    if (!info || idx < 0 || idx >= info->nmidlets)
        return -1;
    memcpy(info->name, info->midlets[idx].name, sizeof info->name);
    memcpy(info->icon, info->midlets[idx].icon, sizeof info->icon);
    memcpy(info->cls, info->midlets[idx].cls, sizeof info->cls);
    return 0;
}

int midlet_has_desc(const midlet_info *info)
{
    return info && info->nmidlets > 0;
}

int manifest_parse_text(const char *text, size_t len, midlet_info *out)
{
    char line[256];
    size_t i = 0;

    if (!out)
        return FX_ERR_FORMAT;
    memset(out, 0, sizeof *out);

    while (i < len) {
        size_t llen = 0;
        int more = 1;

        /* One logical line, with RFC 822-style continuations. */
        while (more && i < len) {
            more = 0;
            while (i < len && text[i] != '\n' && text[i] != '\r') {
                char c = text[i++];
                if (llen + 1 < sizeof line)
                    line[llen++] = c;
            }
            if (i < len && text[i] == '\r') i++;
            if (i < len && text[i] == '\n') i++;
            if (i < len && text[i] == ' ') {   /* continuation line */
                i++;
                more = 1;
            }
        }
        while (llen > 0 && (line[llen - 1] == ' ' || line[llen - 1] == '\t'))
            llen--;
        line[llen] = 0;
        if (llen == 0)
            break;                       /* blank line ends the main section */

        {
            char *colon = memchr(line, ':', llen);
            size_t klen, voff;
            const char *key, *val;
            size_t vlen;

            if (!colon)
                continue;
            klen = (size_t)(colon - line);
            key = line;
            *colon = 0;                  /* NUL-terminate the key */
            voff = klen + 1;
            if (voff < llen && line[voff] == ' ')
                voff++;                  /* single space after ':' */
            val = line + voff;
            vlen = llen - voff;
            trim(&key, &klen);

            /* every attribute is queryable via MIDlet.getAppProperty */
            add_attr(out, key, klen, val, vlen);

            if (strcmp(key, "MIDlet-Name") == 0)
                copy_bounded(out->midlet_name, sizeof out->midlet_name,
                             val, vlen);
            else if (strcmp(key, "MIDlet-Vendor") == 0)
                copy_bounded(out->vendor, sizeof out->vendor, val, vlen);
            else if (strcmp(key, "MIDlet-Version") == 0)
                copy_bounded(out->version, sizeof out->version, val, vlen);
        }
    }

    build_midlets(out);
    out->jar_size = attr_u32(manifest_attr(out, "MIDlet-Jar-Size"));
    out->data_size = attr_u32(manifest_attr(out, "MIDlet-Data-Size"));

    return out->nattrs > 0 ? FX_OK : FX_ERR_FORMAT;
}

void manifest_merge(const midlet_info *base, const midlet_info *over,
                    midlet_info *out)
{
    size_t used = 0;
    int i;

    memset(out, 0, sizeof *out);

    /* Descriptor attributes first: they take precedence, and duplicates in
     * the merged list would make getAppProperty order-dependent. */
    if (over) {
        for (i = 0; i < over->nattrs; i++) {
            const char *e = over->attrs + used;
            const char *eq = strchr(e, '=');
            if (eq)
                add_attr(out, e, (size_t)(eq - e), eq + 1, strlen(eq + 1));
            used += strlen(e) + 1;
        }
    }
    if (base) {
        used = 0;
        for (i = 0; i < base->nattrs; i++) {
            const char *e = base->attrs + used;
            const char *eq = strchr(e, '=');
            if (eq) {
                char key[256];
                size_t klen = (size_t)(eq - e);
                if (klen < sizeof key) {
                    memcpy(key, e, klen);
                    key[klen] = 0;
                    if (!manifest_has(out, key))
                        add_attr(out, key, klen, eq + 1, strlen(eq + 1));
                }
            }
            used += strlen(e) + 1;
        }
    }

    if (over && over->midlet_name[0])
        memcpy(out->midlet_name, over->midlet_name, sizeof out->midlet_name);
    else if (base)
        memcpy(out->midlet_name, base->midlet_name, sizeof out->midlet_name);

    if (over && over->vendor[0])
        memcpy(out->vendor, over->vendor, sizeof out->vendor);
    else if (base)
        memcpy(out->vendor, base->vendor, sizeof out->vendor);

    if (over && over->version[0])
        memcpy(out->version, over->version, sizeof out->version);
    else if (base)
        memcpy(out->version, base->version, sizeof out->version);

    out->jar_size = (over && over->jar_size) ? over->jar_size
                   : (base ? base->jar_size : 0);
    out->data_size = (over && over->data_size) ? over->data_size
                     : (base ? base->data_size : 0);

    /* The descriptor may carry the MIDlet entries instead of the manifest. */
    build_midlets(out);
}

int manifest_parse(const jar *j, midlet_info *out)
{
    int idx = jar_find(j, "META-INF/MANIFEST.MF");
    uint32_t usize;
    void *buf;
    size_t got = 0;
    int r;

    if (idx < 0) {
        /* case-insensitive fallback */
        int i;
        for (i = 0; i < j->nentries; i++) {
            const char *n = j->entries[i].name;
            const char *want = "meta-inf/manifest.mf";
            int k = 0, hit = 1;
            for (; want[k]; k++) {
                char c = n[k];
                if (c >= 'A' && c <= 'Z')
                    c += 32;
                if (c != want[k]) {
                    hit = 0;
                    break;
                }
            }
            if (hit && n[k] == 0) {
                idx = i;
                break;
            }
        }
    }
    if (idx < 0)
        return FX_ERR_NOTFOUND;

    usize = j->entries[idx].usize;
    if (usize == 0)
        return FX_ERR_FORMAT;
    if (usize > 32768)
        usize = 32768;                   /* manifests are small */
    buf = malloc(usize);
    if (!buf)
        return FX_ERR_NOSPACE;
    r = jar_extract(j, idx, buf, usize, &got);
    if (r == FX_OK)
        r = manifest_parse_text((const char *)buf, got, out);
    free(buf);
    return r;
}
