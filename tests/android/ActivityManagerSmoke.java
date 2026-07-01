import android.app.Activity;
import android.app.ActivityManager;
import java.util.List;

public final class ActivityManagerSmoke {
    public static void main(String[] args) {
        ActivityManager manager =
            new Activity().getSystemService(ActivityManager.class);
        List<ActivityManager.RunningAppProcessInfo> processes =
            manager.getRunningAppProcesses();
        boolean found = false;
        for (ActivityManager.RunningAppProcessInfo process : processes) {
            if (args[0].equals(process.processName) && process.pid > 0)
                found = true;
        }
        if (!found) throw new AssertionError("running process not found");
        if (manager.getAppTasks().isEmpty())
            throw new AssertionError("running task not found");
        System.out.println("runningProcesses=" + processes.size());
        System.out.println("runningTasks=" + manager.getAppTasks().size());
    }
}
