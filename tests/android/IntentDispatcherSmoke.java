import android.content.Intent;
import android.content.pm.PackageManager;
import com.muplar.runtime.IntentDispatcher;

public final class IntentDispatcherSmoke {
    public static void main(String[] args) throws Exception {
        PackageManager packageManager = new PackageManager();
        Intent intent = packageManager.getLaunchIntentForPackage(args[0]);
        if (intent == null)
            throw new AssertionError("launch Intent not found for " + args[0]);
        IntentDispatcher.launch(intent, packageManager);
    }
}
