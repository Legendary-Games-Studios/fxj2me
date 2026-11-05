package javax.microedition.lcdui;

public class Image {
    /* 1-bit pixels live in a GC-managed byte[]: row-major, MSB-first,
     * stride (width+7)/8, bit set = black (draw-core sense). No native
     * handle: freed images are reclaimed by the GC like any object. */
    private byte[] pix;
    private int width;
    private int height;
    private boolean mutable;

    private Image(int width, int height, boolean mutable) {
        this.width = width;
        this.height = height;
        this.mutable = mutable;
        this.pix = new byte[((width + 7) / 8) * height];
    }

    public static Image createImage(int width, int height) {
        return new Image(width, height, true);
    }

    /* PNG decode is a native (it needs the inflate core). */
    public static Image createImage(byte[] imageData, int imageOffset,
                                    int imageLength) {
        return decodePNG(imageData, imageOffset, imageLength);
    }
    public static Image createImage(String name) throws java.io.IOException {
        throw new java.io.IOException("resource images need jar access");
    }
    public static Image createImage(Image source) {
        Image img = new Image(source.width, source.height, false);
        System.arraycopy(source.pix, 0, img.pix, 0, source.pix.length);
        return img;
    }

    /* MIDP: a fresh Graphics (default state) per call. */
    public Graphics getGraphics() {
        if (!mutable)
            throw new IllegalStateException("immutable image");
        return new Graphics(this);
    }

    public int getWidth() { return width; }
    public int getHeight() { return height; }
    public boolean isMutable() { return mutable; }

    public native void getRGB(int[] rgbData, int offset, int scanlength,
                              int x, int y, int width, int height);

    private static native Image decodePNG(byte[] data, int off, int len);
}
