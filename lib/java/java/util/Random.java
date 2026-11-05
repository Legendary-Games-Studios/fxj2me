package java.util;

public class Random {
    private long seed;

    public Random() { this(System.currentTimeMillis()); }
    public Random(long seed) { setSeed(seed); }

    public void setSeed(long seed) {
        this.seed = (seed ^ 0x5DEECE66DL) & ((1L << 48) - 1);
    }

    private int next(int bits) {
        seed = (seed * 0x5DEECE66DL + 0xBL) & ((1L << 48) - 1);
        return (int)(seed >>> (48 - bits));
    }

    public int nextInt() { return next(32); }
    public int nextInt(int bound) {
        if (bound <= 0) throw new IllegalArgumentException("bound");
        int r = next(31);
        int m = bound - 1;
        if ((bound & m) == 0) return (int)((bound * (long)r) >> 31);
        int u;
        do {
            u = next(31);
        } while (u + m - (r = u % bound) < 0);
        return r;
    }
    public long nextLong() {
        return ((long)next(32) << 32) + next(32);
    }
    public boolean nextBoolean() { return next(1) != 0; }
    public float nextFloat() { return next(24) / (float)(1 << 24); }
    public double nextDouble() {
        return (((long)next(26) << 27) + next(27)) / (double)(1L << 53);
    }

    /* Shared instance behind Math.random(). */
    static Random staticRandom = new Random(0x12345678L);
    public static double staticNextDouble() {
        return staticRandom.nextDouble();
    }
}
