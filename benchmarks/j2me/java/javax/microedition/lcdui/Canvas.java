package javax.microedition.lcdui;

/** Manual scheduling: the benchmark driver invokes paint once per captured frame. */
public abstract class Canvas extends Displayable {
    public static final int UP = 1, LEFT = 2, RIGHT = 5, DOWN = 6, FIRE = 8, GAME_A = 9,
                            GAME_B = 10, GAME_C = 11, GAME_D = 12;
    public static final int KEY_NUM0 = 48, KEY_NUM1 = 49, KEY_NUM2 = 50, KEY_NUM3 = 51,
                            KEY_NUM4 = 52, KEY_NUM5 = 53, KEY_NUM6 = 54, KEY_NUM7 = 55,
                            KEY_NUM8 = 56, KEY_NUM9 = 57, KEY_STAR = 42, KEY_POUND = 35;
    protected abstract void paint(Graphics g);
    public void setFullScreenMode(boolean b) {}

    public void repaint() {}

    public void repaint(int x, int y, int w, int h) {}

    public void serviceRepaints() {}

    protected void keyPressed(int key) {}

    protected void keyReleased(int key) {}

    protected void keyRepeated(int key) {}

    protected void sizeChanged(int w, int h) {}

    protected void showNotify() {}

    protected void hideNotify() {}

    public int getGameAction(int key) {
        switch (key) {
            case -1:
            case 50:
                return UP;
            case -2:
            case 56:
                return DOWN;
            case -3:
            case 52:
                return LEFT;
            case -4:
            case 54:
                return RIGHT;
            case -5:
            case 53:
                return FIRE;
            default:
                return 0;
        }
    }

    public int getKeyCode(int action) {
        return -action;
    }

    public String getKeyName(int key) {
        return Integer.toString(key);
    }

    public boolean hasPointerEvents() {
        return false;
    }

    public boolean hasPointerMotionEvents() {
        return false;
    }

    public boolean hasRepeatEvents() {
        return true;
    }

    public boolean isDoubleBuffered() {
        return true;
    }
}
