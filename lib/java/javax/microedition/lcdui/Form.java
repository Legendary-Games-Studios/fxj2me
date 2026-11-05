package javax.microedition.lcdui;

import java.util.Vector;

/* A screen holding a vertical list of items. Rendering shows one row per
 * item ("label ... value"); up/down move the selection and EXE edits a
 * TextField in place (digits type, DEL/LEFT delete, EXE commits). */
public class Form extends Displayable {
    private Vector items;
    private int selected;
    private int editing = -1;

    public Form(String title) {
        this.title = title;
        items = new Vector();
    }

    public Form(String title, Item[] elements) {
        this(title);
        if (elements != null)
            for (int i = 0; i < elements.length; i++)
                append(elements[i]);
    }

    public int append(Item item) {
        items.addElement(item);
        return items.size() - 1;
    }

    public int append(String str) {
        return append(new StringItem(null, str));
    }

    public void insert(int itemNum, Item item) {
        items.insertElementAt(item, itemNum);
    }

    public void set(int itemNum, Item item) {
        items.setElementAt(item, itemNum);
    }

    public Item get(int itemNum) {
        return (Item)items.elementAt(itemNum);
    }

    public void delete(int itemNum) {
        items.removeElementAt(itemNum);
        if (editing == itemNum)
            editing = -1;
        if (selected >= items.size())
            selected = items.size() - 1;
        if (selected < 0)
            selected = 0;
    }

    public void deleteAll() {
        items.removeAllElements();
        selected = 0;
        editing = -1;
    }

    public int size() { return items.size(); }

    /*--- rendering -----------------------------------------------------*/

    public void callPaint(Graphics g) {
        int w = getWidth();
        int h = getHeight();
        int fh = g.getFont().getHeight();
        int rowH = fh + 3;
        int top = fh + 3;
        int visible = (h - top) / rowH;
        int scroll = selected - visible + 1;
        int n = items.size();
        int i;

        if (scroll < 0)
            scroll = 0;

        g.setColor(0xffffff);
        g.fillRect(0, 0, w, h);
        g.setColor(0x000000);
        if (title != null)
            g.drawString(title, 1, 1, Graphics.TOP | Graphics.LEFT);
        g.drawLine(0, top - 2, w - 1, top - 2);

        for (i = scroll; i < n && i < scroll + visible; i++) {
            Item it = (Item)items.elementAt(i);
            int y = top + (i - scroll) * rowH;
            boolean sel = (i == selected);
            String val = it.valueText();

            if (sel) {
                g.setColor(0x000000);
                g.fillRect(0, y, w, rowH);
                g.setColor(0xffffff);
            } else {
                g.setColor(0x000000);
            }
            if (it.getLabel() != null)
                g.drawString(it.getLabel(), 2, y + 1,
                             Graphics.TOP | Graphics.LEFT);
            if (i == editing)
                val = val + "_";
            if (val != null && val.length() > 0) {
                int vw = g.getFont().stringWidth(val);
                g.drawString(val, w - 2 - vw, y + 1,
                             Graphics.TOP | Graphics.LEFT);
            }
        }
    }

    /*--- input ---------------------------------------------------------*/

    public void callKeyPressed(int code) {
        if (editing >= 0)
            editKey(code);
        else if (code == Canvas.UP_KEY)
            select(selected - 1);
        else if (code == Canvas.DOWN_KEY)
            select(selected + 1);
        else if (code == Canvas.SELECT_KEY)
            beginEdit();
        Display.requestRepaint(this);
    }

    private void select(int i) {
        if (i >= 0 && i < items.size())
            selected = i;
    }

    private void beginEdit() {
        if (items.size() == 0)
            return;
        Item it = (Item)items.elementAt(selected);
        if (it.editable()) {
            editing = selected;
            it.editBegin();
        }
    }

    private void editKey(int code) {
        Item it = (Item)items.elementAt(editing);
        if (code == Canvas.SELECT_KEY) {
            editing = -1;
        } else if (code == Canvas.UP_KEY || code == Canvas.DOWN_KEY) {
            editing = -1;
            select(selected + (code == Canvas.DOWN_KEY ? 1 : -1));
        } else if (code == Canvas.CLEAR_KEY || code == Canvas.LEFT_KEY) {
            it.editDelete();
        } else if (code >= '0' && code <= '9') {
            it.editChar((char)code);
        }
    }
}
