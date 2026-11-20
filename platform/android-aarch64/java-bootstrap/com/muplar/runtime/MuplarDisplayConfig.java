package com.muplar.runtime;

/** Shared display dimensions for resources, window services and software frames. */
final class MuplarDisplayConfig {
    private static final int WIDTH = dimension("MUPLAR_ANDROID_DISPLAY_WIDTH", 1080);
    private static final int HEIGHT = dimension("MUPLAR_ANDROID_DISPLAY_HEIGHT", 1920);

    private MuplarDisplayConfig() {}

    static int widthPixels() { return WIDTH; }
    static int heightPixels() { return HEIGHT; }

    private static int dimension(String name, int fallback) {
        try {
            int value = Integer.parseInt(System.getenv(name));
            return value >= 160 && value <= 4096 ? value : fallback;
        } catch (RuntimeException ignored) {
            return fallback;
        }
    }
}
