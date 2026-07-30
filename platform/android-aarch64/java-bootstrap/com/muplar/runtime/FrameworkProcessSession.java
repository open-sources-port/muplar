package com.muplar.runtime;

public final class FrameworkProcessSession {
    // Must match muplar::services::Opcode::AppSession in
    // services/muplard_protocol.h.
    private static final int OPCODE_APP_SESSION = 9;

    private static volatile MuplarSocketClient session;
    private FrameworkProcessSession() {}

    public static synchronized void start(String packageName,
                                          String activityName) {
        if (session != null || packageName == null || packageName.isEmpty()) return;
        String socket = System.getProperty("muplar.service.socket", "");
        if (socket.isEmpty()) return;
        MuplarSocketClient client = MuplarSocketClient.connect(socket);
        if (client == null) {
            System.err.println("[ActivityManager] session failed: could not connect");
            return;
        }
        // muplard keys the running-task registry (query-tasks) to this
        // connection's fd, so the session must stay open for the app's
        // lifetime, same as the process this replaces used to.
        if (!client.sendFrame(OPCODE_APP_SESSION,
                packageName + "\n" + (activityName == null ? "" : activityName))) {
            System.err.println("[ActivityManager] session failed: could not register");
            client.close();
            return;
        }
        session = client;
        Runtime.getRuntime().addShutdownHook(new Thread(new Runnable() {
            @Override public void run() {
                MuplarSocketClient current = session;
                if (current != null) current.close();
            }
        }, "muplar-app-session-shutdown"));
    }
}
