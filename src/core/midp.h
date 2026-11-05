/* fxj2me: MIDP runtime — MIDlet launcher, event pump, lcdui natives.
 *
 * The pump drives one MIDlet against the 128x64 monochrome frame. Key
 * codes are Canvas key codes (UP_KEY..SELECT_KEY = -1..-5, phone keys
 * KEY_NUM0.. = 48..). All entry points are host-portable; the device
 * layer only supplies keys and presents midp_frame(). */
#ifndef FXJ2ME_MIDP_H
#define FXJ2ME_MIDP_H

#include "vm.h"
#include "manifest.h"

#define MIDP_FRAME_W    128
#define MIDP_FRAME_H    64
#define MIDP_FRAME_SIZE (MIDP_FRAME_W * MIDP_FRAME_H / 8)

/* Key event kinds for midp_key(). */
enum {
    MIDP_KEY_UP = 0,
    MIDP_KEY_DOWN = 1,
    MIDP_KEY_REPEAT = 2,
};

/* Boot the MIDlet described by `info`: register the lcdui natives, build
 * the screen, construct the MIDlet (manifest properties installed), and
 * call startApp(). The VM must be initialized with a boot class library
 * and the app jar. */
int midp_start(vm *v, jthread *t, const midlet_info *info);

/* One pump step: runs a pending callSerially Runnable, services repaints,
 * and notices notifyDestroyed(). Returns FX_OK while the MIDlet runs, or
 * FX_ERR_JAVA when application code threw uncaught (midp_error() has the
 * text; the MIDlet is marked finished). */
int midp_poll(void);

/* Deliver a key event to the current Canvas. */
int midp_key(int code, int kind);

/* Fire the idx-th Command of the current Displayable (soft keys). */
int midp_softkey(int idx);

/* The rendered frame: 128x64 1bpp, row-major, MSB-first, bit = black. */
const uint8_t *midp_frame(void);

/* Nonzero once the MIDlet has been destroyed (or failed). */
int midp_finished(void);

/* Call destroyApp(true) if still live, then release C-side state. */
void midp_stop(void);

/* Text of the last uncaught exception / startup failure ("" if none). */
const char *midp_error(void);

#endif /* FXJ2ME_MIDP_H */
