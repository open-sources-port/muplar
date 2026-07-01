package android.app;

import android.content.Context;
import android.content.Intent;
import android.content.IntentSender;
import android.os.Bundle;

public final class PendingIntent {
    public static final int FLAG_UPDATE_CURRENT = 1 << 27;
    public static final int FLAG_IMMUTABLE = 1 << 26;
    private final Context context;
    private final Intent intent;

    private PendingIntent(Context context, Intent intent) {
        this.context = context;
        this.intent = intent;
    }
    public static PendingIntent getActivity(Context context, int requestCode,
            Intent intent, int flags) { return new PendingIntent(context, intent); }
    public static PendingIntent getActivity(Context context, int requestCode,
            Intent intent, int flags, Bundle options) {
        return new PendingIntent(context, intent);
    }
    public static PendingIntent getBroadcast(Context context, int requestCode,
            Intent intent, int flags) { return new PendingIntent(context, intent); }
    public void send() throws CanceledException {
        if (context == null || intent == null) throw new CanceledException();
        context.startActivity(intent);
    }
    public IntentSender getIntentSender() { return new IntentSender(); }

    public static class CanceledException extends Exception {
        public CanceledException() { super(); }
    }
}
