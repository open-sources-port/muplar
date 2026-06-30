package android.os;

import java.util.Collections;
import java.util.ArrayList;
import java.util.List;
import com.muplar.runtime.FrameworkServiceClient;

public class UserManager {
    public boolean isUserUnlocked() { return true; }
    public boolean isSystemUser() { return UserHandle.myUserId() == 0; }
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
