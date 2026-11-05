/* fxj2me: application management software (AMS) — phone-style suite install.
 *
 * Mirrors what a MIDP device's AMS does (MIDP 2.0, "MIDlet Suite Packaging" /
 * "Application Management"):
 *
 *   - A suite is one JAR plus an optional JAD descriptor. The descriptor's
 *     attributes override the manifest's.
 *   - Install validates MIDlet-Name, MIDlet-Version, MIDlet-Vendor, the
 *     MIDlet-<n> list (consecutive from 1), MIDlet-Jar-Size against the real
 *     file size, the required profiles, and that every declared MIDlet class
 *     is present in the JAR.
 *   - Installed suites live under their own directory keyed by a device
 *     assigned suite ID: <root>/<id>/APP.JAR, APP.JAD, SUITE.MF, DATA/.
 *     Re-installing the same name+vendor upgrades in place (same ID).
 *   - Uninstall removes the whole suite namespace, including its data.
 *
 * The record (SUITE.MF) is the merged main-section attributes, so listing and
 * launching never needs to re-open the JAR.
 */
#ifndef FXJ2ME_AMS_H
#define FXJ2ME_AMS_H

#include "manifest.h"

#include <stddef.h>

#define AMS_MAX_SUITES 24
#define AMS_ERR_MAX    64

#define AMS_JAR_FILE   "APP.JAR"
#define AMS_JAD_FILE   "APP.JAD"
#define AMS_REC_FILE   "SUITE.MF"
#define AMS_DATA_DIR   "DATA"

typedef struct ams_suite {
    int id;
    char name[MF_NAME_MAX];    /* MIDlet-Name   */
    char version[16];          /* MIDlet-Version */
    char vendor[MF_NAME_MAX];  /* MIDlet-Vendor  */
    int nmidlets;
    uint32_t jar_size;
    uint32_t data_size;        /* MIDlet-Data-Size */
} ams_suite;

typedef struct ams_list {
    ams_suite *items;          /* caller-provided storage */
    int count;
    int cap;
    int truncated;
} ams_list;

void ams_list_init(ams_list *l, ams_suite *items, int cap);

/* Suite directory / jar path for suite `id`. Returns 0 or -1 if it does not
 * fit in `cap`. */
int ams_suite_dir(char *dst, size_t cap, const char *root, int id);
int ams_jar_path(char *dst, size_t cap, const char *root, int id);

/* Directory holding the jar's unpacked entries (see pkg.h). The runner uses
 * it instead of the jar, so class bytes are read from storage. */
int ams_class_dir(char *dst, size_t cap, const char *root, int id);

/* Install (or upgrade) the suite in `jar_path`. `jad_path` is optional; when
 * given its attributes take precedence and MIDlet-Jar-Size is enforced.
 * Returns FX_OK, or a negative FX_* code with a human-readable reason in
 * `err`. */
int ams_install(const char *jar_path, const char *jad_path, const char *root,
                ams_suite *out, char *err, size_t errcap);

/* List installed suites, sorted by name. Returns 0 or -1. */
int ams_list_suites(const char *root, ams_list *out);

/* Remove the suite and everything under its namespace. */
int ams_uninstall(const char *root, int id);

/* Load a suite record (merged attributes). Returns FX_OK or an error. */
int ams_load_suite(const char *root, int id, midlet_info *out);

/* Load a suite record with MIDlet `midx` (0-based) selected for launch. */
int ams_load_midlet(const char *root, int id, int midx, midlet_info *out);

#endif /* FXJ2ME_AMS_H */
