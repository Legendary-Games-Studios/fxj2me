package javax.microedition.lcdui;

import java.util.Vector;

public abstract class Displayable {
    String title;
    private Vector commands;
    private CommandListener listener;

    Displayable() { commands = new Vector(); }

    public String getTitle() { return title; }
    public void setTitle(String title) { this.title = title; }

    public void addCommand(Command cmd) {
        if (!commands.contains(cmd)) commands.addElement(cmd);
    }
    public void removeCommand(Command cmd) { commands.removeElement(cmd); }
    public void setCommandListener(CommandListener l) { listener = l; }

    public int getWidth() { return Display.getDisplayWidth(); }
    public int getHeight() { return Display.getDisplayHeight(); }
    public boolean isShown() { return Display.getCurrent() == this; }

    /* Launcher hooks. Canvas overrides the rendering/event methods with
     * its own; Alert and Form provide their own paint and key handling. */
    public void callPaint(Graphics g) { }
    public void callKeyPressed(int code) { }
    public void callKeyReleased(int code) { }
    public void callKeyRepeated(int code) { }

    public final int commandCount() { return commands.size(); }
    public final Command commandAt(int i) { return (Command)commands.get(i); }
    public final void fireCommand(Command c) {
        if (listener != null) listener.commandAction(c, this);
    }
}
