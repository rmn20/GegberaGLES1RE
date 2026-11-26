package javax.microedition.rms;

/** Benchmarks start from an empty store and never write host user data. */
public final class RecordStore {
    public static RecordStore openRecordStore(String name, boolean create)
        throws RecordStoreException {
        if (!create)
            throw new RecordStoreException("No saved records");
        return new RecordStore();
    }

    public static void deleteRecordStore(String name) throws RecordStoreException {}

    public int getNumRecords() {
        return 0;
    }

    public byte[] getRecord(int id) throws RecordStoreException {
        throw new RecordStoreException("No saved records");
    }

    public int addRecord(byte[] data, int offset, int length) {
        return 1;
    }

    public void closeRecordStore() {}
}
