package javax.microedition.io;

/** The capture host does not open J2ME network connections. */
public final class Connector {
    public static Connection open(String name) throws java.io.IOException {
        throw new java.io.IOException("Network disabled: " + name);
    }
}
