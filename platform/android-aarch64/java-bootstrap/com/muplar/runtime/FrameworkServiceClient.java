package com.muplar.runtime;

public final class FrameworkServiceClient {
    // Opcode values must match muplar::services::Opcode in
    // services/muplard_protocol.h.
    private static final int OPCODE_TAB_FINISHED = 30;

    private FrameworkServiceClient() {}

    public static String request(String operation, String payload) {
        String socket = System.getProperty("muplar.service.socket", "");
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
        return -1;
    }
}
