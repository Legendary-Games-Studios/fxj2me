package java.io;

public abstract class InputStream {
    public abstract int read() throws IOException;
    public int read(byte[] b, int off, int len) throws IOException {
        for (int i = 0; i < len; i++) {
            int c = read();
            if (c < 0) return i == 0 ? -1 : i;
            b[off + i] = (byte)c;
        }
        return len;
    }
    public int available() throws IOException { return 0; }
    public void close() throws IOException { }
}
