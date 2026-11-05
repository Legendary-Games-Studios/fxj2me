package javax.microedition.lcdui;

import javax.microedition.midlet.MIDlet;

public final class Display {
    /* The framebuffer is 1:1 with the physical display in v1. */
    private static final int WIDTH = 128;
    private static final int HEIGHT = 64;

    private static Display display;
    private static Displayable current;
    private static Displayable alertReturn;  /* screen behind an Alert */
    private static boolean repaintRequested;
    private static Runnable pendingRunnable;

    private Display() { }

    public static Display getDisplay(MIDlet m) {
        if (display == null) display = new Display();
        return display;
    }

    public static int getDisplayWidth() { return WIDTH; }
    public static int getDisplayHeight() { return HEIGHT; }

    public void setCurrent(Displayable next) {
        if (next instanceof Alert)
            alertReturn = current;         /* dismiss -> back to this */
        show(next);
    }
    public void setCurrent(Alert alert, Displayable next) {
        alertReturn = next;
        show(alert);
    }
    public static Displayable getCurrent() { return current; }

    /* Dismiss a modal Alert: return to the screen behind it. */
    public static void dismissAlert(Alert a) {
        if (current == a) {
            current = alertReturn;
            alertReturn = null;
            repaintRequested = true;
        }
    }

    private static void show(Displayable next) {
        if (current != null && current instanceof Canvas)
            ((Canvas)current).callHideNotify();
        current = next;
        repaintRequested = true;
        if (next instanceof Canvas)
            ((Canvas)next).callShowNotify();
    }

    public void callSerially(Runnable r) { pendingRunnable = r; }
    public int numColors() { return 2; }
    public boolean isColor() { return false; }
    public boolean isColor0() { return false; }
    public int numAlphaLevels() { return 2; }
    public boolean flashBacklight(int duration) { return false; }
    public boolean vibrate(int duration) { return false; }

    /* --- launcher hooks (called from C) -------------------------------- */
    public static void requestRepaint(Displayable d) {
        repaintRequested = true;
    }
    public static void serviceRepaints() {
        repaintRequested = true;
    }
    public static boolean takeRepaintRequest() {
        boolean r = repaintRequested;
        repaintRequested = false;
        return r;
    }
    public static Runnable takeRunnable() {
        Runnable r = pendingRunnable;
        pendingRunnable = null;
        return r;
    }
    public static int frameWidth() { return WIDTH; }
    public static int frameHeight() { return HEIGHT; }
}
