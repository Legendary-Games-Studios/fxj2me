package javax.microedition.midlet;

public abstract class MIDlet {
    /* Set by the launcher before startApp. */
    private String[] propKeys;
    private String[] propValues;
    private boolean destroyed;

    protected MIDlet() { }

    protected abstract void startApp();
    protected abstract void pauseApp();
    protected abstract void destroyApp(boolean unconditional);

    public final String getAppProperty(String key) {
        if (propKeys == null) return null;
        for (int i = 0; i < propKeys.length; i++)
            if (propKeys[i].equals(key)) return propValues[i];
        return null;
    }

    public final void notifyDestroyed() {
        destroyed = true;
    }
    public final void notifyPaused() { }
    public final void resumeRequest() { }
    public boolean platformRequest(String url) { return false; }
    public final int checkPermission(String permission) { return 1; }

    /* Launcher hooks (called from C). */
    public final void callStartApp() { startApp(); }
    public final void callPauseApp() { pauseApp(); }
    public final void callDestroyApp(boolean unconditional) {
        destroyApp(unconditional);
    }
    public final boolean isDestroyed() { return destroyed; }
    public final void setProperties(String[] keys, String[] values) {
        propKeys = keys;
        propValues = values;
    }
}
