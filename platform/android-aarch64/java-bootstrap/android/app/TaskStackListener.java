package android.app;

import android.content.ComponentName;
import android.window.TaskSnapshot;

public class TaskStackListener implements ITaskStackListener {
    public void onTaskStackChanged() {}
    public void onActivityPinned(String packageName, int userId, int taskId,
            int stackId) {}
    public void onActivityUnpinned() {}
    public void onActivityRestartAttempt(ActivityManager.RunningTaskInfo task,
            boolean homeTaskVisible, boolean clearedTask, boolean wasVisible) {}
    public void onActivityForcedResizable(String packageName, int taskId,
            int reason) {}
    public void onActivityDismissingDockedTask() {}
    public void onActivityLaunchOnSecondaryDisplayFailed(
            ActivityManager.RunningTaskInfo task, int requestedDisplayId) {}
    public void onActivityLaunchOnSecondaryDisplayRerouted(
            ActivityManager.RunningTaskInfo task, int requestedDisplayId) {}
    public void onTaskCreated(int taskId, ComponentName componentName) {}
    public void onTaskRemoved(int taskId) {}
    public void onTaskMovedToFront(ActivityManager.RunningTaskInfo task) {}
    public void onTaskDescriptionChanged(ActivityManager.RunningTaskInfo task) {}
    public void onActivityRequestedOrientationChanged(int taskId,
            int requestedOrientation) {}
    public void onTaskProfileLocked(ActivityManager.RunningTaskInfo task,
            int userId) {}
    public void onTaskSnapshotChanged(int taskId, TaskSnapshot snapshot) {}
    public void onBackPressedOnTaskRoot(ActivityManager.RunningTaskInfo task) {}
    public void onTaskDisplayChanged(int taskId, int newDisplayId) {}
    public void onRecentTaskListUpdated() {}
    public void onRecentTaskListFrozenChanged(boolean frozen) {}
    public void onTaskSnapshotInvalidated(int taskId) {}
    public void onActivityRotation(int displayId) {}
    public void onLockTaskModeChanged(int mode) {}
}
