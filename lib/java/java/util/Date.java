package java.util;

/* A point in time, milliseconds since the Unix epoch. CLDC subset. */
public class Date {
    private long time;

    public Date() { time = System.currentTimeMillis(); }
    public Date(long date) { time = date; }

    public long getTime() { return time; }
    public void setTime(long time) { this.time = time; }

    public boolean before(Date when) { return time < when.time; }
    public boolean after(Date when) { return time > when.time; }

    public boolean equals(Object obj) {
        return (obj instanceof Date) && ((Date)obj).time == time;
    }

    public int hashCode() {
        return (int)(time ^ (time >>> 32));
    }

    public String toString() {
        /* "YYYY-MM-DDTHH:MM:SSZ" (UTC), civil-from-days conversion. */
        char[] b = new char[20];
        long ms = time;
        long s = ms / 1000;
        if (ms < 0 && s * 1000 != ms)
            s--;
        long d = s / 86400;
        long rem = s % 86400;
        if (rem < 0) {
            rem += 86400;
            d--;
        }
        long z = d + 719468;
        long era = (z >= 0 ? z : z - 146096) / 146097;
        long doe = z - era * 146097;
        long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        long y = yoe + era * 400;
        long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        long mp = (5 * doy + 2) / 153;
        long dd = doy - (153 * mp + 2) / 5 + 1;
        long mm = mp + (mp < 10 ? 3 : -9);
        if (mm <= 2)
            y++;

        put4(b, 0, (int)y);
        b[4] = '-';
        put2(b, 5, (int)mm);
        b[7] = '-';
        put2(b, 8, (int)dd);
        b[10] = 'T';
        put2(b, 11, (int)(rem / 3600));
        b[13] = ':';
        put2(b, 14, (int)(rem / 60 % 60));
        b[16] = ':';
        put2(b, 17, (int)(rem % 60));
        b[19] = 'Z';
        return new String(b);
    }

    private static void put2(char[] b, int at, int v) {
        b[at] = (char)('0' + v / 10 % 10);
        b[at + 1] = (char)('0' + v % 10);
    }

    private static void put4(char[] b, int at, int v) {
        put2(b, at, v / 100);
        put2(b, at + 2, v % 100);
    }
}
