package javax.microedition.util;

public final class ContextHolder {
    public static java.io.InputStream getResourceAsStream(Object ignored, String name) {
        return ContextHolder.class.getResourceAsStream(name);
    }
}
