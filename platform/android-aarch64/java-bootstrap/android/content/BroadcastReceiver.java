package android.content;

public abstract class BroadcastReceiver {
    public abstract void onReceive(Context context, Intent intent);
    public boolean isOrderedBroadcast() { return false; }
    public boolean isInitialStickyBroadcast() { return false; }
}
