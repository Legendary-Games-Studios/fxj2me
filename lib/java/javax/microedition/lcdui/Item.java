package javax.microedition.lcdui;

/* Base class for the elements a Form displays. */
public abstract class Item {
    private String label;

    protected Item() { }
    protected Item(String label) { this.label = label; }

    public String getLabel() { return label; }
    public void setLabel(String label) { this.label = label; }

    /* Launcher hooks: Form rendering and in-place editing. */
    String valueText() { return ""; }
    boolean editable() { return false; }
    void editBegin() { }
    void editChar(char c) { }
    void editDelete() { }
}
