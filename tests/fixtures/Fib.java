/* fxj2me test fixture: pure-Java methods covering the interpreter skeleton.
 * Compiled with javac --release 8 by tests/gen_fixtures.py and packed into
 * test.jar. No strings, no exceptions, no reflection — by design. */
public class Fib {
    static int counter;
    static Object keep;

    public static int fib(int n) {
        if (n < 2)
            return n;
        return fib(n - 1) + fib(n - 2);
    }

    public static int sumLoop(int n) {
        int s = 0;
        for (int i = 1; i <= n; i++)
            s += i;
        return s;
    }

    public static long factLong(int n) {
        long f = 1;
        for (int i = 2; i <= n; i++)
            f *= i;
        return f;
    }

    public static int arraySum(int n) {
        int[] a = new int[n];
        int s = 0;
        for (int i = 0; i < n; i++)
            a[i] = i * i;
        for (int i = 0; i < n; i++)
            s += a[i];
        return s;
    }

    public static int bytes(byte[] b) {
        int s = 0;
        for (int i = 0; i < b.length; i++)
            s += b[i];
        return s;
    }

    public static int matrix(int n) {
        int[][] m = new int[n][n];
        int s = 0;
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                m[i][j] = i * j;
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                s += m[i][j];
        return s;
    }

    public static int switchy(int n) {
        switch (n) {
        case 0:  return 100;
        case 5:  return 500;
        case 9:  return 900;
        default: return -1;
        }
    }

    public static int lookupSwitchy(int n) {
        switch (n) {
        case -7:    return 7;
        case 1000:  return 1000;
        case 42:    return 42;
        default:    return 0;
        }
    }

    public static int arithmetic(int a, int b) {
        return (a * b) + (a - b) / (b | 1) - (a ^ b) + (a << 2) + (a >> 1)
             + (a / (b | 1)) + (a & b) - (a % (b | 1)) + (a >>> 1);
    }

    public static float floats(float x) {
        return x * 2.5f + 1.0f;
    }

    public static double doubles(double x, int n) {
        double acc = 0;
        for (int i = 0; i < n; i++)
            acc += x / (i + 1);
        return acc;
    }

    public static int statics(int n) {
        counter += n;
        return counter;
    }

    public static int objects(int n) {
        Cell c = new Cell(n);
        for (int i = 0; i < n; i++)
            c.add(i);
        return c.total;
    }

    public static int wideArgs(long base, int n) {
        long acc = base;
        for (int i = 1; i <= n; i++)
            acc += i * 1000L;
        return (int)(acc >> 4);
    }

    public static int mixes(long l, double d, int n) {
        long acc = l;
        double dd = d;
        for (int i = 0; i < n; i++) {
            acc += (long)(dd * i);
            dd += 0.5;
        }
        return (int)acc;
    }

    static class Cell {
        int total;
        Cell(int t) {
            total = t;
        }
        void add(int x) {
            total += x;
        }
    }
}
