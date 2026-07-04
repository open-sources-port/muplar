package android.os;

import java.util.Collections;
import java.util.ArrayList;
import java.util.List;
import com.muplar.runtime.FrameworkServiceClient;

public class UserManager {
    public boolean isManagedProfile() { return false; }
    public boolean isUserUnlocked() { return true; }
    public boolean isUserUnlocked(UserHandle user) {
        return user != null && getUserProfiles().contains(user);
    }
    public boolean isSystemUser() { return UserHandle.myUserId() == 0; }
    public boolean isDemoUser() { return false; }
    public boolean isQuietModeEnabled(UserHandle user) { return false; }
    public Bundle getUserRestrictions(UserHandle user) { return new Bundle(); }
    public long getSerialNumberForUser(UserHandle user) {
        return user == null ? -1 : user.getIdentifier();
    }
    public UserHandle getUserForSerialNumber(long serialNumber) {
        if (serialNumber < 0 || serialNumber > Integer.MAX_VALUE) return null;
        return new UserHandle((int) serialNumber);
    }
    public List<UserHandle> getUserProfiles() {
        String response = FrameworkServiceClient.request("query-users", "");
        if (response == null || response.isEmpty())
            return Collections.singletonList(UserHandle.CURRENT);
        List<UserHandle> users = new ArrayList<UserHandle>();
        for (String value : response.split(",")) {
            try { users.add(new UserHandle(Integer.parseInt(value.trim()))); }
            catch (NumberFormatException ignored) {}
        }
        return users.isEmpty()
            ? Collections.singletonList(UserHandle.CURRENT) : users;
    }
    public boolean isUserRunning(UserHandle user) {
        return user != null && getUserProfiles().contains(user);
    }
}
