package java.util;

public class Stack extends Vector {
    public Stack() { super(); }
    public Object push(Object item) {
        addElement(item);
        return item;
    }
    public synchronized Object pop() {
        Object obj = peek();
        removeElementAt(size() - 1);
        return obj;
    }
    public synchronized Object peek() {
        int n = size();
        if (n == 0) throw new NoSuchElementException("Stack empty");
        return elementAt(n - 1);
    }
    public boolean empty() { return size() == 0; }
}
