package android.media.permission;

public interface SafeCloseable extends AutoCloseable {
    @Override void close();
}
