import android.app.Activity;
import android.content.Context;
import android.os.UserManager;
import android.provider.Settings;
import android.util.DisplayMetrics;
import android.util.SparseArray;
import android.view.WindowManager;

public final class FrameworkBaselineSmoke {
    public static void main(String[] args) {
        Activity context = new Activity();
        WindowManager windows = context.getSystemService(WindowManager.class);
        DisplayMetrics metrics = new DisplayMetrics();
        windows.getDefaultDisplay().getMetrics(metrics);
        if (metrics.widthPixels <= 0 || metrics.heightPixels <= 0)
            throw new AssertionError("invalid display metrics");
        UserManager users = context.getSystemService(UserManager.class);
        if (!users.isUserUnlocked() || users.getUserProfiles().size() != 1)
            throw new AssertionError("single-user baseline failed");
        Settings.Secure.putString(context.getContentResolver(), "smoke", "ok");
        if (!"ok".equals(Settings.Secure.getString(
                context.getContentResolver(), "smoke")))
            throw new AssertionError("settings baseline failed");
        if (context.checkSelfPermission("android.permission.TEST") != 0)
            throw new AssertionError("permission baseline failed");
        SparseArray<String> sparse = new SparseArray<>();
        sparse.put(9, "nine");
        sparse.put(2, "two");
        if (sparse.keyAt(0) != 2 || !"nine".equals(sparse.get(9)))
            throw new AssertionError("sparse array baseline failed");
        System.out.println("display=" + metrics.widthPixels + "x" +
            metrics.heightPixels);
        System.out.println("singleUser=ok");
        System.out.println("settings=ok");
        System.out.println("permissions=ok");
        System.out.println("sparseArray=ok");
    }
}
