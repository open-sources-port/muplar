package android.os;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public final class FileUtils {
    private FileUtils() {}

    public static long copy(InputStream input, OutputStream output)
            throws IOException {
        byte[] buffer = new byte[8192];
        long total = 0;
        int count;
        while ((count = input.read(buffer)) != -1) {
            output.write(buffer, 0, count);
            total += count;
        }
        return total;
    }
}
