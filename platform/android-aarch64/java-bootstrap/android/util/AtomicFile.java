package android.util;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;

public class AtomicFile {
    private final File baseFile;
    private final File backupFile;

    public AtomicFile(File baseFile) {
        this.baseFile = baseFile;
        this.backupFile = new File(baseFile.getPath() + ".bak");
    }
    public FileInputStream openRead() throws FileNotFoundException {
        if (backupFile.exists()) {
            baseFile.delete();
            backupFile.renameTo(baseFile);
        }
        return new FileInputStream(baseFile);
    }
    public FileOutputStream startWrite() throws IOException {
        File parent = baseFile.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs())
            throw new IOException("cannot create " + parent);
        if (baseFile.exists() && !backupFile.exists() &&
                !baseFile.renameTo(backupFile))
            throw new IOException("cannot back up " + baseFile);
        return new FileOutputStream(baseFile);
    }
    public void finishWrite(FileOutputStream stream) {
        close(stream);
        backupFile.delete();
    }
    public void failWrite(FileOutputStream stream) {
        close(stream);
        baseFile.delete();
        backupFile.renameTo(baseFile);
    }
    private static void close(FileOutputStream stream) {
        if (stream == null) return;
        try { stream.getFD().sync(); } catch (IOException ignored) {}
        try { stream.close(); } catch (IOException ignored) {}
    }
}
