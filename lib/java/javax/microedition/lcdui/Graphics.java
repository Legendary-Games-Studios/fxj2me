package javax.microedition.lcdui;

public class Graphics {
    public static final int HCENTER = 1;
    public static final int VCENTER = 2;
    public static final int LEFT = 4;
    public static final int RIGHT = 8;
    public static final int TOP = 16;
    public static final int BOTTOM = 32;
    public static final int BASELINE = 64;
    public static final int SOLID = 0;
    public static final int DOTTED = 1;

    /* Drawing state. Pixels land in the target Image's native buffer. */
    private Image target;
    private int color = 0x000000;
    private int gray;
    private int translateX, translateY;
    private int clipX, clipY, clipW, clipH;
    private Font font = Font.getDefaultFont();
    private int strokeStyle = SOLID;

    Graphics(Image target) {
        this.target = target;
        clipX = 0;
        clipY = 0;
        clipW = target.getWidth();
        clipH = target.getHeight();
    }

    public int getColor() { return color; }
    public int getGrayScale() { return gray; }
    public void setColor(int red, int green, int blue) {
        color = (red << 16) | (green << 8) | blue;
        gray = (red * 30 + green * 59 + blue * 11) / 100;
    }
    public void setColor(int RGB) {
        color = RGB & 0xffffff;
        setColor((RGB >> 16) & 0xff, (RGB >> 8) & 0xff, RGB & 0xff);
    }
    public void setGrayScale(int value) {
        gray = value & 0xff;
        setColor(gray, gray, gray);
    }

    public Font getFont() { return font; }
    public void setFont(Font font) {
        if (font != null) this.font = font;
    }

    public void setStrokeStyle(int style) { strokeStyle = style; }
    public int getStrokeStyle() { return strokeStyle; }

    public int getTranslateX() { return translateX; }
    public int getTranslateY() { return translateY; }
    public void translate(int x, int y) {
        translateX += x;
        translateY += y;
    }

    public int getClipX() { return clipX - translateX; }
    public int getClipY() { return clipY - translateY; }
    public int getClipWidth() { return clipW; }
    public int getClipHeight() { return clipH; }
    public void setClip(int x, int y, int w, int h) {
        clipX = x + translateX;
        clipY = y + translateY;
        clipW = w;
        clipH = h;
    }
    public void clipRect(int x, int y, int w, int h) {
        int nx = x + translateX, ny = y + translateY;
        int x1 = Math.max(clipX, nx);
        int y1 = Math.max(clipY, ny);
        int x2 = Math.min(clipX + clipW, nx + w);
        int y2 = Math.min(clipY + clipH, ny + h);
        clipX = x1;
        clipY = y1;
        clipW = Math.max(0, x2 - x1);
        clipH = Math.max(0, y2 - y1);
    }

    /* --- drawing (natives render 1-bit via the draw core) --------------- */
    public native void drawLine(int x1, int y1, int x2, int y2);
    public native void drawRect(int x, int y, int width, int height);
    public native void fillRect(int x, int y, int width, int height);
    public native void drawRoundRect(int x, int y, int w, int h,
                                     int arcWidth, int arcHeight);
    public native void fillRoundRect(int x, int y, int w, int h,
                                     int arcWidth, int arcHeight);
    public native void drawArc(int x, int y, int width, int height,
                               int startAngle, int arcAngle);
    public native void fillArc(int x, int y, int width, int height,
                               int startAngle, int arcAngle);
    public native void fillTriangle(int x1, int y1, int x2, int y2,
                                    int x3, int y3);
    public native void drawString(String str, int x, int y, int anchor);
    public native void drawSubstring(String str, int offset, int len,
                                     int x, int y, int anchor);
    public native void drawChar(char ch, int x, int y, int anchor);
    public native void drawImage(Image img, int x, int y, int anchor);
    public native void drawRegion(Image src, int x_src, int y_src,
                                  int width, int height, int transform,
                                  int x_dest, int y_dest, int anchor);
    public native void drawRGB(int[] rgbData, int offset, int scanlength,
                               int x, int y, int width, int height,
                               boolean processAlpha);
}
