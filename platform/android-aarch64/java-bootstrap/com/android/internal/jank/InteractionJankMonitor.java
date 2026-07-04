package com.android.internal.jank;

import android.view.View;

public final class InteractionJankMonitor {
    public static final class Configuration {
        public static final class Builder {
            public static Builder withView(int cujType, View view) {
                return new Builder();
            }
            public Builder setTimeout(long timeoutMillis) { return this; }
            public Builder setTag(String tag) { return this; }
        }
    }
    private static final InteractionJankMonitor INSTANCE =
        new InteractionJankMonitor();

    public static InteractionJankMonitor getInstance() { return INSTANCE; }
    public boolean begin(View view, int cujType) { return false; }
    public boolean begin(Configuration.Builder builder) { return false; }
    public boolean cancel(int cujType) { return false; }
    public boolean end(int cujType) { return false; }
    public boolean isInstrumenting(int cujType) { return false; }
}
