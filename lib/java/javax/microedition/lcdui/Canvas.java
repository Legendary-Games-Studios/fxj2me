package javax.microedition.lcdui;

public abstract class Canvas extends Displayable {
    public static final int UP = 1;
    public static final int DOWN = 6;
    public static final int LEFT = 2;
    public static final int RIGHT = 5;
    public static final int FIRE = 8;
    public static final int GAME_A = 9;
    public static final int GAME_B = 10;
    public static final int GAME_C = 11;
    public static final int GAME_D = 12;

    /* Phone key codes we synthesize from the calculator keypad. */
    public static final int KEY_NUM0 = 48;
    public static final int KEY_NUM1 = 49;
    public static final int KEY_NUM2 = 50;
    public static final int KEY_NUM3 = 51;
    public static final int KEY_NUM4 = 52;
    public static final int KEY_NUM5 = 53;
    public static final int KEY_NUM6 = 54;
    public static final int KEY_NUM7 = 55;
    public static final int KEY_NUM8 = 56;
    public static final int KEY_NUM9 = 57;
    public static final int KEY_STAR = 42;
    public static final int KEY_POUND = 35;
    public static final int UP_KEY = -1;
    public static final int DOWN_KEY = -2;
    public static final int LEFT_KEY = -3;
    public static final int RIGHT_KEY = -4;
    public static final int SELECT_KEY = -5;
    public static final int CLEAR_KEY = -8;   /* phones' clear/backspace */

    protected Canvas() { }

    protected abstract void paint(Graphics g);

    protected void keyPressed(int keyCode) { }
    protected void keyReleased(int keyCode) { }
    protected void keyRepeated(int keyCode) { }
    protected void pointerPressed(int x, int y) { }
    protected void pointerReleased(int x, int y) { }
    protected void pointerDragged(int x, int y) { }
    protected void sizeChanged(int w, int h) { }
    protected void showNotify() { }
    protected void hideNotify() { }

    public void repaint() { Display.requestRepaint(this); }
    public void repaint(int x, int y, int width, int height) {
        Display.requestRepaint(this);
    }
    public void serviceRepaints() { Display.serviceRepaints(); }

    public int getGameAction(int keyCode) {
        switch (keyCode) {
        case UP_KEY:    return UP;
        case DOWN_KEY:  return DOWN;
        case LEFT_KEY:  return LEFT;
        case RIGHT_KEY: return RIGHT;
        case SELECT_KEY:return FIRE;
        /* standard phone layout: 2/4/6/8/5 act as arrows/fire */
        case KEY_NUM2:  return UP;
        case KEY_NUM4:  return LEFT;
        case KEY_NUM6:  return RIGHT;
        case KEY_NUM8:  return DOWN;
        case KEY_NUM5:  return FIRE;
        default: return 0;
        }
    }
    public int getKeyCode(int gameAction) {
        switch (gameAction) {
        case UP: return UP_KEY;
        case DOWN: return DOWN_KEY;
        case LEFT: return LEFT_KEY;
        case RIGHT: return RIGHT_KEY;
        case FIRE: return SELECT_KEY;
        default: return 0;
        }
    }
    public String getKeyName(int keyCode) {
        switch (keyCode) {
        case UP_KEY: return "Up";
        case DOWN_KEY: return "Down";
        case LEFT_KEY: return "Left";
        case RIGHT_KEY: return "Right";
        case SELECT_KEY: return "Select";
        default: return "?";
        }
    }

    public boolean hasPointerEvents() { return false; }
    public boolean hasPointerMotionEvents() { return false; }
    public boolean hasRepeatEvents() { return true; }

    /* Launcher hooks. */
    public final void callPaint(Graphics g) { paint(g); }
    public final void callKeyPressed(int k) { keyPressed(k); }
    public final void callKeyReleased(int k) { keyReleased(k); }
    public final void callKeyRepeated(int k) { keyRepeated(k); }
    public final void callShowNotify() { showNotify(); }
    public final void callHideNotify() { hideNotify(); }
}
