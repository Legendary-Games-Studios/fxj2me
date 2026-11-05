package java.util;

public class Vector {
    private Object[] elementData;
    private int elementCount;

    public Vector() { this(10); }
    public Vector(int initialCapacity) {
        elementData = new Object[initialCapacity > 0 ? initialCapacity : 10];
    }

    private void ensure(int min) {
        if (min > elementData.length) {
            int cap = elementData.length * 2 + 2;
            if (cap < min) cap = min;
            Object[] nd = new Object[cap];
            System.arraycopy(elementData, 0, nd, 0, elementCount);
            elementData = nd;
        }
    }

    public void addElement(Object obj) {
        ensure(elementCount + 1);
        elementData[elementCount++] = obj;
    }
    public boolean add(Object obj) { addElement(obj); return true; }
    public void insertElementAt(Object obj, int index) {
        if (index < 0 || index > elementCount)
            throw new ArrayIndexOutOfBoundsException(index);
        ensure(elementCount + 1);
        System.arraycopy(elementData, index, elementData, index + 1,
                         elementCount - index);
        elementData[index] = obj;
        elementCount++;
    }
    public void setElementAt(Object obj, int index) {
        if (index < 0 || index >= elementCount)
            throw new ArrayIndexOutOfBoundsException(index);
        elementData[index] = obj;
    }
    public void removeElementAt(int index) {
        if (index < 0 || index >= elementCount)
            throw new ArrayIndexOutOfBoundsException(index);
        System.arraycopy(elementData, index + 1, elementData, index,
                         elementCount - index - 1);
        elementCount--;
        elementData[elementCount] = null;
    }
    public boolean removeElement(Object obj) {
        int i = indexOf(obj);
        if (i < 0) return false;
        removeElementAt(i);
        return true;
    }
    public boolean remove(Object obj) { return removeElement(obj); }
    public void removeAllElements() {
        for (int i = 0; i < elementCount; i++) elementData[i] = null;
        elementCount = 0;
    }
    public void setSize(int newSize) {
        if (newSize < 0) throw new ArrayIndexOutOfBoundsException(newSize);
        ensure(newSize);
        for (int i = newSize; i < elementCount; i++) elementData[i] = null;
        elementCount = newSize;
    }

    public Object elementAt(int index) {
        if (index < 0 || index >= elementCount)
            throw new ArrayIndexOutOfBoundsException(index);
        return elementData[index];
    }
    public Object get(int index) { return elementAt(index); }
    public Object firstElement() {
        if (elementCount == 0) throw new NoSuchElementException();
        return elementData[0];
    }
    public Object lastElement() {
        if (elementCount == 0) throw new NoSuchElementException();
        return elementData[elementCount - 1];
    }

    public boolean contains(Object obj) { return indexOf(obj) >= 0; }
    public int indexOf(Object obj) {
        for (int i = 0; i < elementCount; i++) {
            Object e = elementData[i];
            if (obj == null ? e == null : obj.equals(e)) return i;
        }
        return -1;
    }
    public int lastIndexOf(Object obj) {
        for (int i = elementCount - 1; i >= 0; i--) {
            Object e = elementData[i];
            if (obj == null ? e == null : obj.equals(e)) return i;
        }
        return -1;
    }

    public int size() { return elementCount; }
    public boolean isEmpty() { return elementCount == 0; }
    public int capacity() { return elementData.length; }

    public void copyInto(Object[] anArray) {
        System.arraycopy(elementData, 0, anArray, 0, elementCount);
    }
    public Object[] toArray() {
        Object[] r = new Object[elementCount];
        copyInto(r);
        return r;
    }
    public Enumeration elements() {
        return new VectorEnumerator(this);
    }

    static final class VectorEnumerator implements Enumeration {
        final Vector v;
        int i;
        VectorEnumerator(Vector v) { this.v = v; }
        public boolean hasMoreElements() { return i < v.elementCount; }
        public Object nextElement() {
            if (i >= v.elementCount) throw new NoSuchElementException();
            return v.elementData[i++];
        }
    }
}
