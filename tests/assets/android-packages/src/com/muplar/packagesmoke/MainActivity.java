package com.muplar.packagesmoke;

import android.app.Activity;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.VersionedPackage;
import android.os.Bundle;
import java.util.HashSet;

public final class MainActivity extends Activity {
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        try {
            PackageManager pm = getPackageManager();
            PackageInfo self = pm.getPackageInfo(getPackageName(), PackageManager.GET_META_DATA);
            require(self.getLongVersionCode() == 17 && "1.7".equals(self.versionName), "self version");
            require(self.applicationInfo.targetSdkVersion == 28, "self target SDK");
            require(self.applicationInfo.metaData.getInt("package.test") == 42, "self metadata");
            self.applicationInfo.metaData.putInt("package.test", -1);
            require(pm.getPackageInfo(getPackageName(), PackageManager.GET_META_DATA)
                .applicationInfo.metaData.getInt("package.test") == 42, "cache mutation");
            require(pm.getPackageInfo(getPackageName(), 0).applicationInfo.metaData == null,
                "metadata flags ignored");
            require(pm.getInstalledModules(0).isEmpty(), "unmanaged modules were invented");
            try { pm.getModuleInfo("com.muplar.absent", 0); throw new AssertionError("unknown module returned"); }
            catch (PackageManager.NameNotFoundException expected) {}
            check(pm, "com.android.vending", 83032110);
            check(pm, "com.google.android.gms", 214857039);
            check(pm, "com.google.android.gsf", 30);
            HashSet<String> names = new HashSet<>();
            for (PackageInfo info : pm.getInstalledPackages(0)) {
                require(names.add(info.packageName), "duplicate package");
            }
            require(names.size() == 4 && names.contains(getPackageName())
                && names.contains("com.google.android.gms"), "enumeration inconsistent");
            HashSet<String> apps = new HashSet<>();
            for (ApplicationInfo info : pm.getInstalledApplications(0)) {
                require(apps.add(info.packageName), "duplicate application");
            }
            require(apps.equals(names), "application enumeration inconsistent");
            require(pm.checkSignatures("com.android.vending", "com.google.android.gms")
                == PackageManager.SIGNATURE_MATCH, "Google signer comparison");
            require(pm.checkSignatures(getPackageName(), "com.android.vending")
                == PackageManager.SIGNATURE_NO_MATCH, "different signer falsely matches Google");
            require(pm.checkSignatures("com.muplar.absent", "com.android.vending")
                == PackageManager.SIGNATURE_UNKNOWN_PACKAGE, "unknown signer falsely matches");
            missing(pm, "com.muplar.absent");
            missing(pm, "com.muplar.wrongidentity");
            try {
                pm.getPackageInfo(new VersionedPackage("com.google.android.gms", 1), 0);
                throw new AssertionError("version constraint ignored");
            } catch (PackageManager.NameNotFoundException expected) {}
            System.out.println("PACKAGE_DISCOVERY_SMOKE_PASS versions=real signers=verified metadata=isolated");
        } catch (PackageManager.NameNotFoundException e) {
            throw new AssertionError(e);
        }
    }

    private static void check(PackageManager pm, String name, long version)
            throws PackageManager.NameNotFoundException {
        PackageInfo info = pm.getPackageInfo(name, PackageManager.GET_SIGNATURES
            | PackageManager.GET_SIGNING_CERTIFICATES | PackageManager.GET_META_DATA);
        require(info.getLongVersionCode() == version, "version wrong: " + name);
        require(info.signatures != null && info.signatures.length == 1, "signer missing: " + name);
        require(info.signingInfo != null && info.signingInfo.getApkContentsSigners().length == 1,
            "SigningInfo missing: " + name);
        require(info.applicationInfo.sourceDir.endsWith(".apk"), "APK path missing: " + name);
        require(pm.getApplicationInfo(name, 0).targetSdkVersion == info.applicationInfo.targetSdkVersion,
            "application lookup inconsistent");
        info.signatures[0] = null;
        require(pm.getPackageInfo(name, PackageManager.GET_SIGNATURES).signatures[0] != null,
            "signer cache mutation");
        System.out.println("PACKAGE_DISCOVERY_PACKAGE " + name + " version=" + version);
    }

    private static void missing(PackageManager pm, String name) {
        try {
            pm.getPackageInfo(name, 0);
            throw new AssertionError("unknown/mismatched package returned: " + name);
        } catch (PackageManager.NameNotFoundException expected) {}
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
}
