package android.content.pm;

import android.content.ComponentName;
import android.content.Intent;
import android.os.IInterface;
import android.os.RemoteException;
import java.util.List;

public interface IPackageManager extends IInterface {
    ActivityInfo getActivityInfo(ComponentName component, long flags, int userId)
        throws RemoteException;
    ComponentName getHomeActivities(List<ResolveInfo> activities)
        throws RemoteException;
    ResolveInfo resolveIntent(Intent intent, String resolvedType, long flags,
        int userId) throws RemoteException;
}
