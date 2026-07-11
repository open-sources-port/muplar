package android.app.admin;

import android.content.Context;
import java.util.function.Supplier;

public final class ParcelableResource {
    private ParcelableResource() {
    }

    public static String loadDefaultString(Supplier<String> defaultStringLoader) {
        return defaultStringLoader == null ? "" : defaultStringLoader.get();
    }

    public String getString(Context context, Supplier<String> defaultStringLoader) {
        return loadDefaultString(defaultStringLoader);
    }

    public String getString(Context context,
                            Supplier<String> defaultStringLoader,
                            Object... formatArgs) {
        return loadDefaultString(defaultStringLoader);
    }
}
