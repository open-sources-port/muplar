package android.view;

import android.graphics.Point;
import android.graphics.Rect;
import android.app.ActivityManager;

public class RemoteAnimationTarget {
    public int taskId;
    public int mode;
    public SurfaceControl leash;
    public boolean isTranslucent;
    public Rect clipRect = new Rect();
    public Rect contentInsets = new Rect();
    public int prefixOrderIndex;
    public Point position = new Point();
    public Rect localBounds = new Rect();
    public Rect screenSpaceBounds = new Rect();
    public boolean isNotInRecents;
    public SurfaceControl startLeash;
    public Rect startBounds = new Rect();
    public ActivityManager.RunningTaskInfo taskInfo;
}
