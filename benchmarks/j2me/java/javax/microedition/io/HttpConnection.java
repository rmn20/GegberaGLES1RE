package javax.microedition.io;

public interface HttpConnection extends InputConnection {
    String getHeaderField(String name) throws java.io.IOException;
}
