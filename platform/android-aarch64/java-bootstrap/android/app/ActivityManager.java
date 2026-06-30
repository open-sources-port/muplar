package android.app;

import android.content.ComponentName;
import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class ActivityManager {
    public List<RunningAppProcessInfo> getRunningAppProcesses() {
        List<RunningAppProcessInfo> result = new ArrayList<RunningAppProcessInfo>();
        for (String[] task : queryTasks()) {
            RunningAppProcessInfo info = new RunningAppProcessInfo();
            info.processName = task[0];
            info.pid = parseInt(task[2]);
            info.uid = parseInt(task[3]);
            info.importance = RunningAppProcessInfo.IMPORTANCE_FOREGROUND;
            result.add(info);
        }
        return result;
    }

    public List<AppTask> getAppTasks() {
        List<AppTask> result = new ArrayList<AppTask>();
        for (String[] task : queryTasks())
            result.add(new AppTask(new ComponentName(task[0], task[1])));
        return result;
    }

    private List<String[]> queryTasks() {
        String executable = System.getProperty("muplar.service.executable", "");
        String socket = System.getProperty("muplar.service.socket", "");
        if (executable.isEmpty() || socket.isEmpty() ||
            !new File(executable).isFile()) return Collections.emptyList();
        List<String[]> result = new ArrayList<String[]>();
        try {
            Process process = new ProcessBuilder(executable, "--socket", socket,
                "--client", "query-tasks")
                .redirectError(ProcessBuilder.Redirect.INHERIT).start();
            try (BufferedReader input = new BufferedReader(
                     new InputStreamReader(process.getInputStream(), "UTF-8"))) {
                String line;
                while ((line = input.readLine()) != null) {
                    String[] fields = line.split("\\t", -1);
                    if (fields.length == 4) result.add(fields);
                }
            }
            process.waitFor();
        } catch (Exception error) {
            System.err.println("[ActivityManager] query failed: " + error);
        }
        return result;
    }

    private static int parseInt(String value) {
        try { return Integer.parseInt(value); }
        catch (NumberFormatException ignored) { return 0; }
    }

    public static class RunningAppProcessInfo {
        public static final int IMPORTANCE_FOREGROUND = 100;
        public String processName;
        public int pid;
        public int uid;
        public int importance;
    }

    public static class AppTask {
        private final RecentTaskInfo taskInfo = new RecentTaskInfo();
        AppTask(ComponentName topActivity) { taskInfo.topActivity = topActivity; }
        public RecentTaskInfo getTaskInfo() { return taskInfo; }
    }

    public static class RecentTaskInfo {
        public ComponentName topActivity;
    }
}
