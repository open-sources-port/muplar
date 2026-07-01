package android.content;

import android.os.Parcel;
import android.os.Parcelable;
import com.muplar.runtime.IntentDispatcher;

public class IntentSender implements Parcelable {
    public static class SendIntentException extends Exception {
        public SendIntentException() {}
        public SendIntentException(String message) { super(message); }
        public SendIntentException(Exception cause) { super(cause); }
    }

    private final Intent intent;
    public IntentSender() { this(null); }
    public IntentSender(Intent intent) { this.intent = intent; }
    public void sendIntent(Context context, int code, Intent fillInIntent,
            OnFinished onFinished, android.os.Handler handler)
            throws SendIntentException {
        Intent target = fillInIntent != null ? fillInIntent : intent;
        if (context == null || target == null)
            throw new SendIntentException("No target intent");
        IntentDispatcher.launch(target, context.getPackageManager());
        if (onFinished != null)
            onFinished.onSendFinished(this, target, 0, null, null);
    }
    public interface OnFinished {
        void onSendFinished(IntentSender sender, Intent intent, int resultCode,
            String resultData, android.os.Bundle resultExtras);
    }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel destination, int flags) {}
}
