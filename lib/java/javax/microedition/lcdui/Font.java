package javax.microedition.lcdui;

public final class Font {
    public static final int FACE_SYSTEM = 0;
    public static final int FACE_MONOSPACE = 32;
    public static final int FACE_PROPORTIONAL = 64;
    public static final int STYLE_PLAIN = 0;
    public static final int STYLE_BOLD = 1;
    public static final int STYLE_ITALIC = 2;
    public static final int STYLE_UNDERLINED = 4;
    public static final int SIZE_SMALL = 8;
    public static final int SIZE_MEDIUM = 0;
    public static final int SIZE_LARGE = 16;

    /* The renderer has a single 6x8 bitmap font; all metrics come from it. */
    private static final int GLYPH_W = 6;
    private static final int GLYPH_H = 8;

    private int face, style, size;
    private static Font defaultFont =
        new Font(FACE_SYSTEM, STYLE_PLAIN, SIZE_MEDIUM);

    private Font(int face, int style, int size) {
        this.face = face;
        this.style = style;
        this.size = size;
    }

    public static Font getDefaultFont() { return defaultFont; }
    public static Font getFont(int face, int style, int size) {
        return defaultFont;
    }

    public int getFace() { return face; }
    public int getStyle() { return style; }
    public int getSize() { return size; }
    public int getHeight() { return GLYPH_H; }
    public int getBaselinePosition() { return 6; }
    public int charWidth(char ch) { return GLYPH_W; }
    public int stringWidth(String str) {
        return str == null ? 0 : str.length() * GLYPH_W;
    }
    public int substringWidth(String str, int offset, int len) {
        return len * GLYPH_W;
    }
}
