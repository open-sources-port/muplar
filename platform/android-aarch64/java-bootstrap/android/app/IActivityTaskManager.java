package android.app;

import android.os.RemoteException;

public interface IActivityTaskManager {
    void registerTaskStackListener(ITaskStackListener listener)
        throws RemoteException;
    void unregisterTaskStackListener(ITaskStackListener listener)
        throws RemoteException;
    ActivityTaskManager.RootTaskInfo getRootTaskInfo(int windowingMode,
        int activityType) throws RemoteException;
}
