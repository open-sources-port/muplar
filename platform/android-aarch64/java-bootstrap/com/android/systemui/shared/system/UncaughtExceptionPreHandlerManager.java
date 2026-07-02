package com.android.systemui.shared.system;

import java.lang.Thread.UncaughtExceptionHandler;

public class UncaughtExceptionPreHandlerManager {
    public UncaughtExceptionPreHandlerManager() {}
    public void registerHandler(UncaughtExceptionHandler handler) {
        // stub: bypass non-existent Thread.getUncaughtExceptionPreHandler
    }
}
