package com.muplar.runtime;

import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;

public final class FrameworkServiceClient {
    private FrameworkServiceClient() {}

    public static String request(String operation, String payload) {
        String executable = System.getProperty("muplar.service.executable", "");
        String socket = System.getProperty("muplar.service.socket", "");
        if (executable.isEmpty() || socket.isEmpty() ||
            !new File(executable).isFile()) return null;
        try {
            Process process = new ProcessBuilder(executable, "--socket", socket,
                "--client", operation, "--payload", payload == null ? "" : payload)
                .redirectError(ProcessBuilder.Redirect.INHERIT).start();
            StringBuilder result = new StringBuilder();
            try (BufferedReader input = new BufferedReader(
                     new InputStreamReader(process.getInputStream(), "UTF-8"))) {
                String line;
                while ((line = input.readLine()) != null) {
                    if (result.length() != 0) result.append('\n');
                    result.append(line);
                }
            }
            return process.waitFor() == 0 ? result.toString() : null;
        } catch (Exception error) {
            System.err.println("[FrameworkServiceClient] " + operation +
                " failed: " + error);
            return null;
        }
    }
}
