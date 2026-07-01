import android.app.Activity;
import android.content.pm.PackageManager;
import android.os.UserHandle;
import android.os.UserManager;
import java.util.List;

public final class FrameworkPolicySmoke {
    public static void main(String[] args) {
        Activity context = new Activity();
        PackageManager packages = context.getPackageManager();
        boolean expectCamera = args.length != 0 && "grant".equals(args[0]);
        boolean camera = packages.checkPermission("android.permission.CAMERA",
            "com.muplar.policytest") == PackageManager.PERMISSION_GRANTED;
        if (camera != expectCamera)
            throw new AssertionError("camera policy mismatch");
        if (packages.checkPermission("android.permission.INTERNET",
                "com.muplar.policytest") != PackageManager.PERMISSION_GRANTED)
            throw new AssertionError("normal permission denied");
        List<UserHandle> profiles = context.getSystemService(UserManager.class)
            .getUserProfiles();
        if (profiles.size() != 2 || profiles.get(0).getIdentifier() != 0 ||
            profiles.get(1).getIdentifier() != 10)
            throw new AssertionError("profile isolation list mismatch");
        if (UserHandle.myUserId() != 10)
            throw new AssertionError("current user mismatch");
        System.out.println("camera=" + (camera ? "granted" : "denied"));
        System.out.println("profiles=0,10 current=10");
    }
}
