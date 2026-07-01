package com.muplar.runtime;

import java.io.File;

public final class FrameworkProcessSession {
    private static volatile Process session;
    private FrameworkProcessSession() {}

    public static synchronized void start(String packageName,
                                          String activityName) {
        if (session != null || packageName == null || packageName.isEmpty()) return;
        String executable = System.getProperty("muplar.service.executable", "");
        String socket = System.getProperty("muplar.service.socket", "");
        if (executable.isEmpty() || socket.isEmpty() ||
            !new File(executable).isFile()) return;
        try {
            session = new ProcessBuilder(executable, "--socket", socket,
                "--client", "app-session", "--payload",
                packageName + "\n" + (activityName == null ? "" : activityName))
                .redirectError(ProcessBuilder.Redirect.INHERIT).start();
            Runtime.getRuntime().addShutdownHook(new Thread(new Runnable() {
                @Override public void run() {
                    Process process = session;
                    if (process != null) process.destroy();
                }
            }, "muplar-app-session-shutdown"));
        } catch (Exception error) {
            System.err.println("[ActivityManager] session failed: " + error);
        }
    }
}
