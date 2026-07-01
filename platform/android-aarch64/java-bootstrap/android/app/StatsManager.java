package android.app;

import android.util.StatsEvent;
import java.util.List;
import java.util.concurrent.Executor;

public final class StatsManager {
    public static final int PULL_SUCCESS = 0;
    public static final int PULL_SKIP = 1;
    public interface StatsPullAtomCallback {
        int onPullAtom(int atomTag, List<StatsEvent> data);
    }
    public void setPullAtomCallback(int atomTag, PullAtomMetadata metadata,
            Executor executor, StatsPullAtomCallback callback) {}
    public void clearPullAtomCallback(int atomTag) {}
    public static final class PullAtomMetadata {
        private PullAtomMetadata() {}
        public static final class Builder {
            public Builder setCoolDownMillis(long value) { return this; }
            public Builder setTimeoutMillis(long value) { return this; }
            public Builder setAdditiveFields(int[] fields) { return this; }
            public PullAtomMetadata build() { return new PullAtomMetadata(); }
        }
    }
}
