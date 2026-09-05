package com.muplar.runtime;

public final class FrameworkServiceClient {
    // Opcode values must match muplar::services::Opcode in
    // services/muplard_protocol.h.
    private static final int OPCODE_TAB_FINISHED = 30;
    private static final int OPCODE_QUERY_PACKAGES = 8;

    private FrameworkServiceClient() {}

    public static String getServiceSocket() {
        String socket = System.getProperty("muplar.service.socket", "");
        if (socket != null && !socket.isEmpty()) return socket;
        socket = System.getenv("MUPLAR_SERVICE_SOCKET");
        if (socket != null && !socket.isEmpty()) return socket;
        String userHome = System.getProperty("user.home", "");
        if (userHome != null && !userHome.isEmpty()) {
            java.io.File candidate = new java.io.File(
                userHome + "/.muplar/prefixes/android-arm64/run/muplard.sock");
            if (candidate.exists()) {
                return candidate.getAbsolutePath();
            }
        }
        return "";
    }

    public static String request(String operation, String payload) {
        String socket = getServiceSocket();
        if (socket.isEmpty())
            return null;
        int opcode = opcodeFor(operation);
        if (opcode < 0)
            return null;
        String reply = MuplarSocketClient.request(socket, opcode, payload);
        if (reply == null)
            System.err.println("[FrameworkServiceClient] " + operation +
                " failed");
        return reply;
    }

    private static int opcodeFor(String operation) {
        if ("tab-finished".equals(operation))
            return OPCODE_TAB_FINISHED;
        if ("query-packages".equals(operation))
            return OPCODE_QUERY_PACKAGES;
        return -1;
    }
}
