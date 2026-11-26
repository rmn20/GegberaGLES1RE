package javax.microedition.lcdui;

public class Displayable {
    public int getWidth() {
        return Integer.getInteger("bench.width", 320);
    }

    public int getHeight() {
        return Integer.getInteger("bench.height", 240);
    }

    public void addCommand(Command c) {}

    public void removeCommand(Command c) {}

    public void setCommandListener(CommandListener l) {}

    public void setTitle(String s) {}

    public boolean isShown() {
        return true;
    }
}
