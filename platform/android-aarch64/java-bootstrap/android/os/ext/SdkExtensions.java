package android.os.ext;

import java.util.Collections;
import java.util.Map;

public class SdkExtensions {
    public static final int AD_SERVICES = 1000000;

    public static int getExtensionVersion(int sdkExtension) {
        return 0;
    }

    public static Map<Integer, Integer> getAllExtensionVersions() {
        return Collections.emptyMap();
    }
}
