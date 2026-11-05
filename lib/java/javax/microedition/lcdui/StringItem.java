package javax.microedition.lcdui;

/* An immutable text entry on a Form. */
public class StringItem extends Item {
    private String text;

    public StringItem(String label, String text) {
        super(label);
        this.text = text;
    }

    public StringItem(String label, String text, int appearanceMode) {
        this(label, text);
    }

    public String getText() { return text; }
    public void setText(String text) { this.text = text; }

    String valueText() { return text == null ? "" : text; }
}
