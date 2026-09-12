package android.content.pm;

import java.util.Collections;
import java.util.List;

public class MuplarPackageInstaller extends PackageInstaller {
    public MuplarPackageInstaller() {
        super();
    }

    @Override
    public List<SessionInfo> getMySessions() {
        return Collections.emptyList();
    }

    @Override
    public List<SessionInfo> getAllSessions() {
        return Collections.emptyList();
    }

    @Override
    public List<SessionInfo> getStagedSessions() {
        return Collections.emptyList();
    }

    @Override
    public SessionInfo getSessionInfo(int sessionId) {
        return null;
    }
}
