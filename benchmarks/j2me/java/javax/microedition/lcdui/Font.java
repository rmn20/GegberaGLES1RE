package javax.microedition.lcdui;

public final class Font {
    public static final int FACE_SYSTEM = 0, FACE_MONOSPACE = 32, FACE_PROPORTIONAL = 64,
                            STYLE_PLAIN = 0, STYLE_BOLD = 1, STYLE_ITALIC = 2, STYLE_UNDERLINED = 4,
                            SIZE_SMALL = 8, SIZE_MEDIUM = 0, SIZE_LARGE = 16;
    public static Font getDefaultFont() {
        return new Font();
    }

    public static Font getFont(int face, int style, int size) {
        return new Font();
    }

    public int getHeight() {
        return 12;
    }

    public int getBaselinePosition() {
        return 10;
    }

    public int charWidth(char c) {
        return 6;
    }

    public int stringWidth(String s) {
        return s.length() * 6;
    }

    public int substringWidth(String s, int off, int len) {
        return len * 6;
    }

    public int charsWidth(char[] s, int off, int len) {
        return len * 6;
    }
}
