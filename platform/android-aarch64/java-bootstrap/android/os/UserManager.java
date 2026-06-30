package android.os;

import java.util.Collections;
import java.util.List;

public class UserManager {
    public boolean isUserUnlocked() { return true; }
    public boolean isSystemUser() { return true; }
    public List<UserHandle> getUserProfiles() {
        return Collections.singletonList(UserHandle.CURRENT);
    }
}
