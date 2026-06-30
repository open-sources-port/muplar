import android.content.pm.PackageManager;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class PackageDaemonCallbackSmoke {
    public static void main(String[] args) throws Exception {
        PackageManager packageManager = new PackageManager();
        final CountDownLatch changed = new CountDownLatch(1);
        packageManager.registerPackageChangeListener(new Runnable() {
            @Override public void run() {
                System.out.println("[PackageDaemonSmoke] callback received");
                changed.countDown();
            }
        });
        if (!changed.await(8, TimeUnit.SECONDS))
            throw new AssertionError("muplard package callback timed out");
    }
}
