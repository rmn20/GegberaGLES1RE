package javax.microedition.lcdui;

import javax.microedition.midlet.MIDlet;

public final class Display {
    private static final Display display = new Display();
    private Displayable current;
    public static Display getDisplay(MIDlet m) {
        return display;
    }

    public void setCurrent(Displayable d) {
        current = d;
    }

    public Displayable getCurrent() {
        return current;
    }

    public void callSerially(Runnable r) {}

    public boolean vibrate(int duration) {
        return false;
    }

    public boolean flashBacklight(int duration) {
        return false;
    }

    public int numColors() {
        return 65536;
    }

    public boolean isColor() {
        return true;
    }
}
