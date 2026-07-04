package android.os;

public class OperationCanceledException extends RuntimeException {
    public OperationCanceledException() { super("operation canceled"); }
}
