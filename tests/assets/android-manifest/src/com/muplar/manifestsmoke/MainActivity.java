package com.muplar.manifestsmoke;

import android.app.Activity;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.util.TypedValue;

public final class MainActivity extends Activity {
    @Override
    public void onCreate(Bundle saved) {
        super.onCreate(saved);
        try {
            Class<?> protocol = Class.forName("org.apache.http.ProtocolVersion", true, getClassLoader());
            Object version = protocol.getConstructor(String.class, int.class, int.class)
                .newInstance("HTTP", 1, 1);
            require("HTTP/1.1".equals(version.toString()), "declared shared library unavailable");
            ApplicationInfo info = getPackageManager().getApplicationInfo(
                getPackageName(), PackageManager.GET_META_DATA);
            android.content.pm.PackageInfo archive = getPackageManager().getPackageArchiveInfo(
                info.sourceDir, PackageManager.GET_META_DATA);
            require(archive != null && getPackageName().equals(archive.packageName),
                    "framework APK archive parsing unavailable");
            require(archive.applicationInfo.targetSdkVersion == 28, "archive SDK wrong");
            Bundle metadata = info.metaData;
            require(metadata != null, "metadata missing");
            require(metadata.getInt("test.integer") == 42, "integer reference not resolved");
            int resource = metadata.getInt("test.resource");
            require(resource != 42 && getResources().getInteger(resource) == 42,
                    "resource ID not preserved");
            require("manifest value".equals(metadata.getString("test.string")), "string missing");
            require(metadata.getBoolean("test.boolean"), "boolean missing");
            require(!metadata.containsKey("activity.only"), "activity metadata leaked");
            require(info.theme == android.R.style.Theme_Material_Light, "application theme wrong");
            TypedValue value = new TypedValue();
            require(getTheme().resolveAttribute(android.R.attr.windowActionBar, value, true)
                    && value.data == 0, "activity theme override missing");
            System.out.println("MANIFEST_SMOKE_PASS metadata=typed theme=activity_override");
        } catch (ReflectiveOperationException | PackageManager.NameNotFoundException e) {
            throw new AssertionError(e);
        }
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
}
