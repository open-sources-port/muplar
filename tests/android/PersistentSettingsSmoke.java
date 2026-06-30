import android.app.Activity;
import android.os.SystemProperties;
import android.provider.Settings;

public final class PersistentSettingsSmoke {
    public static void main(String[] args) {
        Activity context = new Activity();
        if ("write".equals(args[0])) {
            if (!Settings.Secure.putString(context.getContentResolver(),
                    "muplar.smoke", "persisted"))
                throw new AssertionError("settings write failed");
            SystemProperties.set("muplar.smoke", "property-persisted");
            System.out.println("write=ok");
        } else {
            String setting = Settings.Secure.getString(
                context.getContentResolver(), "muplar.smoke");
            String property = SystemProperties.get("muplar.smoke", "missing");
            if (!"persisted".equals(setting) ||
                !"property-persisted".equals(property))
                throw new AssertionError("persistent values missing");
            System.out.println("read=ok");
        }
    }
}
