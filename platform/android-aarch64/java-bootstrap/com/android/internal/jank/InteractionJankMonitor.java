package com.android.internal.jank;

import android.view.View;

public final class InteractionJankMonitor {
    private static final InteractionJankMonitor INSTANCE =
        new InteractionJankMonitor();

    public static InteractionJankMonitor getInstance() { return INSTANCE; }
    public boolean begin(View view, int cujType) { return false; }
    public boolean cancel(int cujType) { return false; }
    public boolean end(int cujType) { return false; }
}
