package com.muplar.runtime;

import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public final class IntentDispatcher {
    private IntentDispatcher() {}

    public static Process launch(Intent intent, PackageManager packageManager) {
        if (intent == null || intent.getComponentPackage() == null) {
            throw new IllegalArgumentException("explicit Intent is required");
        }

        final ApplicationInfo app;
        try {
            app = packageManager.getApplicationInfo(
                intent.getComponentPackage(), 0);
        } catch (PackageManager.NameNotFoundException error) {
            throw new IllegalArgumentException(error.getMessage(), error);
        }
        if (app.sourceDir == null || app.sourceDir.isEmpty() ||
            !new File(app.sourceDir).isFile()) {
            throw new IllegalStateException(
                "APK is unavailable for " + app.packageName);
        }

        String executable = System.getProperty("muplar.launcher.executable", "");
        String prefix = System.getProperty("muplar.prefix.name", "");
        if (executable.isEmpty() || prefix.isEmpty()) {
            throw new IllegalStateException(
                "Muplar launch context is unavailable");
        }

        List<String> command = new ArrayList<String>();
        command.add(executable);
        command.add("--quiet");
        command.add("--prefix");
        command.add(prefix);
        command.add("--apk");
        command.add("--host-window");
        command.add(app.sourceDir);
        try {
            ProcessBuilder processBuilder = new ProcessBuilder(command)
                .redirectInput(ProcessBuilder.Redirect.INHERIT)
                .redirectOutput(ProcessBuilder.Redirect.INHERIT)
                .redirectError(ProcessBuilder.Redirect.INHERIT);
            processBuilder.environment().put("MUPLAR_DISPATCHED_APP", "1");
            Process process = processBuilder.start();
            System.out.println("[IntentDispatcher] launched " +
                app.packageName + "/" + intent.getComponentClass() +
                " from " + app.sourceDir);
            return process;
        } catch (IOException error) {
            throw new IllegalStateException(
                "unable to launch " + app.packageName, error);
        }
    }
}
