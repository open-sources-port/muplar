package android.app;

import android.util.StatsEvent;

import java.util.List;
import java.util.concurrent.Executor;

public class StatsManager {
    public StatsManager() {
    }

    public interface StatsPullAtomCallback {
        int PULL_SUCCESS = 0;
        int PULL_SKIP = 1;

        int onPullAtom(int atomTag, List<StatsEvent> data);
    }

    public static final class PullAtomMetadata {
        private PullAtomMetadata() {
        }

        public static final class Builder {
            public Builder() {
            }

            public Builder setCoolDownMillis(long value) {
                return this;
            }

            public Builder setTimeoutMillis(long value) {
                return this;
            }

            public Builder setAdditiveFields(int[] value) {
                return this;
            }

            public PullAtomMetadata build() {
                return new PullAtomMetadata();
            }
        }
    }

    public void setPullAtomCallback(int atomTag,
                                    PullAtomMetadata metadata,
                                    Executor executor,
                                    StatsPullAtomCallback callback) {
    }

    public void clearPullAtomCallback(int atomTag) {
    }
}
