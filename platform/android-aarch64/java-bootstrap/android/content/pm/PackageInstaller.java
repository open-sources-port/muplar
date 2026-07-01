package android.content.pm;

import android.os.Handler;
import android.os.UserHandle;
import java.util.Collections;
import java.util.List;

public class PackageInstaller {
    public SessionInfo getSessionInfo(int sessionId) { return null; }
    public List<SessionInfo> getAllSessions() { return Collections.emptyList(); }
    public void registerSessionCallback(SessionCallback callback) {}
    public void registerSessionCallback(SessionCallback callback, Handler handler) {}
    public void unregisterSessionCallback(SessionCallback callback) {}

    public abstract static class SessionCallback {
        public abstract void onCreated(int sessionId);
        public abstract void onBadgingChanged(int sessionId);
        public abstract void onActiveChanged(int sessionId, boolean active);
        public abstract void onProgressChanged(int sessionId, float progress);
        public abstract void onFinished(int sessionId, boolean success);
    }

    public static class SessionInfo {
        public int getSessionId() { return -1; }
        public UserHandle getUser() { return UserHandle.of(0); }
        public String getInstallerPackageName() { return null; }
        public String getAppPackageName() { return null; }
        public float getProgress() { return 0f; }
        public boolean isActive() { return false; }
    }
}
