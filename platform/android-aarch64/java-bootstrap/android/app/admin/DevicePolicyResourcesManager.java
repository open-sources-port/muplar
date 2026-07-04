package android.app.admin;

import java.util.function.Supplier;

public class DevicePolicyResourcesManager {
    public String getString(String id, Supplier<String> defaultValue) {
        return defaultValue == null ? "" : defaultValue.get();
    }
}
