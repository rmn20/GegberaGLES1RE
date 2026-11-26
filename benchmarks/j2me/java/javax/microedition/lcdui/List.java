package javax.microedition.lcdui;

public class List extends Displayable implements Choice {
    public List(String s, int type, String[] strings, Image[] images) {}

    public List(String s, int type) {}

    public void setSelectCommand(Command c) {}

    public int getSelectedIndex() {
        return 0;
    }
}
