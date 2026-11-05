package java.util;

public class Hashtable {
    static final class Entry {
        int hash;
        Object key;
        Object value;
        Entry next;
    }

    private Entry[] table;
    private int count;

    public Hashtable() { this(17); }
    public Hashtable(int initialCapacity) {
        table = new Entry[initialCapacity > 0 ? initialCapacity : 17];
    }

    private static int hash(Object key) {
        int h = key.hashCode();
        return h < 0 ? -h : h;
    }

    public Object put(Object key, Object value) {
        if (key == null || value == null)
            throw new NullPointerException();
        int h = hash(key);
        int idx = h % table.length;
        for (Entry e = table[idx]; e != null; e = e.next) {
            if (e.hash == h && e.key.equals(key)) {
                Object old = e.value;
                e.value = value;
                return old;
            }
        }
        if (count + 1 > table.length * 3 / 4) rehash();
        idx = hash(key) % table.length;
        Entry e = new Entry();
        e.hash = h;
        e.key = key;
        e.value = value;
        e.next = table[idx];
        table[idx] = e;
        count++;
        return null;
    }

    private void rehash() {
        Entry[] old = table;
        table = new Entry[old.length * 2 + 1];
        for (int i = 0; i < old.length; i++) {
            Entry e = old[i];
            while (e != null) {
                Entry next = e.next;
                int idx = e.hash % table.length;
                e.next = table[idx];
                table[idx] = e;
                e = next;
            }
        }
    }

    public Object get(Object key) {
        if (key == null) return null;
        int h = hash(key);
        for (Entry e = table[h % table.length]; e != null; e = e.next)
            if (e.hash == h && e.key.equals(key)) return e.value;
        return null;
    }

    public Object remove(Object key) {
        if (key == null) return null;
        int h = hash(key);
        int idx = h % table.length;
        Entry prev = null;
        for (Entry e = table[idx]; e != null; prev = e, e = e.next) {
            if (e.hash == h && e.key.equals(key)) {
                if (prev == null) table[idx] = e.next;
                else prev.next = e.next;
                count--;
                return e.value;
            }
        }
        return null;
    }

    public boolean containsKey(Object key) { return get(key) != null; }
    public boolean contains(Object value) { return containsValue(value); }
    public boolean containsValue(Object value) {
        for (int i = 0; i < table.length; i++)
            for (Entry e = table[i]; e != null; e = e.next)
                if (value == null ? e.value == null : value.equals(e.value))
                    return true;
        return false;
    }

    public int size() { return count; }
    public boolean isEmpty() { return count == 0; }

    public void clear() {
        for (int i = 0; i < table.length; i++) table[i] = null;
        count = 0;
    }

    public Enumeration keys() {
        return new EntryEnumeration(this, true);
    }
    public Enumeration elements() {
        return new EntryEnumeration(this, false);
    }

    static final class EntryEnumeration implements Enumeration {
        final Hashtable h;
        final boolean wantKeys;
        int bucket;
        Entry e;
        EntryEnumeration(Hashtable h, boolean wantKeys) {
            this.h = h;
            this.wantKeys = wantKeys;
        }
        public boolean hasMoreElements() {
            while (e == null) {
                if (bucket >= h.table.length) return false;
                e = h.table[bucket++];
            }
            return true;
        }
        public Object nextElement() {
            if (!hasMoreElements()) throw new NoSuchElementException();
            Object r = wantKeys ? e.key : e.value;
            e = e.next;
            return r;
        }
    }
}
