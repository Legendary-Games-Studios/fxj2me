package javax.microedition.lcdui;

/* Editable text placed on a Form. */
public class TextField extends Item {
    public static final int ANY = 0;
    public static final int EMAILADDR = 1;
    public static final int NUMERIC = 2;
    public static final int PHONENUMBER = 3;
    public static final int URL = 4;
    public static final int DECIMAL = 5;

    public static final int PASSWORD = 0x10000;
    public static final int UNEDITABLE = 0x20000;
    public static final int SENSITIVE = 0x40000;
    public static final int NON_PREDICTIVE = 0x80000;
    public static final int INITIAL_CAPS_WORD = 0x100000;
    public static final int INITIAL_CAPS_SENTENCE = 0x200000;

    private String text;
    private int maxSize;
    private int constraints;
    private boolean fresh;

    public TextField(String label, String text, int maxSize, int constraints) {
        super(label);
        this.text = (text == null) ? "" : text;
        this.maxSize = maxSize > 0 ? maxSize : 0;
        this.constraints = constraints;
        if (this.text.length() > this.maxSize)
            this.text = this.text.substring(0, this.maxSize);
    }

    public String getString() { return text; }

    public void setString(String text) {
        this.text = (text == null) ? "" : text;
        if (this.text.length() > maxSize)
            this.text = this.text.substring(0, maxSize);
        fresh = false;
    }

    public int size() { return text.length(); }

    public int getMaxSize() { return maxSize; }

    public void setMaxSize(int maxSize) {
        this.maxSize = maxSize > 0 ? maxSize : 0;
        if (text.length() > this.maxSize)
            text = text.substring(0, this.maxSize);
    }

    public int getConstraints() { return constraints; }
    public void setConstraints(int constraints) { this.constraints = constraints; }

    /* Launcher hooks: Form rendering and in-place editing. */
    String valueText() { return text; }

    boolean editable() { return (constraints & UNEDITABLE) == 0; }

    void editBegin() { fresh = true; }

    void editChar(char c) {
        int mode = constraints & 0xff;
        if (mode == NUMERIC && (c < '0' || c > '9'))
            return;
        if (mode == DECIMAL && !((c >= '0' && c <= '9') || c == '.'))
            return;
        if (fresh) {
            text = "";
            fresh = false;
        }
        if (text.length() < maxSize)
            text = text + c;
    }

    void editDelete() {
        if (text.length() > 0)
            text = text.substring(0, text.length() - 1);
        fresh = false;
    }
}
