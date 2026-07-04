package android.os;

public final class CancellationSignal {
    private boolean canceled;
    public boolean isCanceled() { return canceled; }
    public void cancel() { canceled = true; }
    public void throwIfCanceled() {
        if (canceled) throw new OperationCanceledException();
    }
}
