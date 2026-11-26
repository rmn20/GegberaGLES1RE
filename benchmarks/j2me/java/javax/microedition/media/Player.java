package javax.microedition.media;

public interface Player {
    void start() throws MediaException;
    void stop() throws MediaException;
    void close();
    void realize() throws MediaException;
    void prefetch() throws MediaException;
    void deallocate();
    void setLoopCount(int count);
    long setMediaTime(long now) throws MediaException;
}
