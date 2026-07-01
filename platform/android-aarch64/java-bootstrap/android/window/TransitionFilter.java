package android.window;

import android.content.ComponentName;
import android.os.IBinder;

public class TransitionFilter {
    public int[] mTypeSet = new int[0];
    public int mFlags;
    public int mNotFlags;
    public Requirement[] mRequirements = new Requirement[0];

    public static class Requirement {
        public int mActivityType;
        public int[] mModes = new int[0];
        public boolean mMustBeTask;
        public int mOrder;
        public ComponentName mTopActivity;
        public IBinder mLaunchCookie;
        public int mWindowingMode;
        public int mFlags;
    }
}
