package javax.microedition.lcdui;

public class Alert extends Displayable {
    public static final int FOREVER = -2;

    private String text;
    private int timeout = 2000;

    public Alert(String title) { this.title = title; }
    public Alert(String title, String alertText) {
        this.title = title;
        this.text = alertText;
    }
    public Alert(String title, String alertText, Image alertImage,
                 AlertType alertType) {
        this.title = title;
        this.text = alertText;
    }
    public void setString(String text) { this.text = text; }
    public String getString() { return text; }
    public void setTimeout(int time) { timeout = time; }
    public int getTimeout() { return timeout; }
    public void setType(AlertType type) { }

    /* Launcher hooks: paint the alert; any key dismisses it. */
    public void callPaint(Graphics g) {
        int w = getWidth();
        int h = getHeight();
        int fh = g.getFont().getHeight();
        int per = w / g.getFont().charWidth(' ');
        int y = fh + 3;
        int i = 0;

        g.setColor(0xffffff);
        g.fillRect(0, 0, w, h);
        g.setColor(0x000000);
        if (title != null) {
            g.drawString(title, 1, 1, Graphics.TOP | Graphics.LEFT);
            g.drawLine(0, y - 2, w - 1, y - 2);
        }
        if (text != null) {
            while (i < text.length() && y + fh <= h) {
                int n = 0;
                while (i + n < text.length() && n < per &&
                       text.charAt(i + n) != '\n')
                    n++;
                if (n > 0)
                    g.drawSubstring(text, i, n, 2, y,
                                    Graphics.TOP | Graphics.LEFT);
                i += n;
                if (i < text.length() && text.charAt(i) == '\n')
                    i++;
                y += fh;
            }
        }
    }

    public void callKeyPressed(int code) {
        Display.dismissAlert(this);
    }
}
