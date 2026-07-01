package android.app;

import android.content.Context;
import android.os.Handler;
import java.util.concurrent.CopyOnWriteArrayList;

public final class WallpaperManager {
    public static final int FLAG_SYSTEM = 1;
    public static final int FLAG_LOCK = 2;
    public interface OnColorsChangedListener {
        void onColorsChanged(WallpaperColors colors, int which);
    }
    private static final WallpaperManager INSTANCE = new WallpaperManager();
    private final CopyOnWriteArrayList<OnColorsChangedListener> listeners =
        new CopyOnWriteArrayList<>();
    private WallpaperManager() {}
    public static WallpaperManager getInstance(Context context) { return INSTANCE; }
    public WallpaperColors getWallpaperColors(int which) {
        return new WallpaperColors();
    }
    public void addOnColorsChangedListener(
            OnColorsChangedListener listener, Handler handler) {
        if (listener != null) listeners.addIfAbsent(listener);
    }
    public void removeOnColorsChangedListener(OnColorsChangedListener listener) {
        listeners.remove(listener);
    }
    public boolean isWallpaperSupported() { return false; }
    public boolean isSetWallpaperAllowed() { return false; }
    public int getDesiredMinimumWidth() { return 1800; }
    public int getDesiredMinimumHeight() { return 1169; }
    public void suggestDesiredDimensions(int minimumWidth, int minimumHeight) {}
}
