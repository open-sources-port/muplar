import android.app.Activity;
import android.os.Bundle;
import android.view.View;
import java.lang.reflect.Method;
import javax.swing.JButton;
import javax.swing.JLabel;
import javax.swing.SwingUtilities;

public final class UiInteractionSmoke {
    public static void main(String[] args) throws Exception {
        Class<?> activityClass = Class.forName("com.muplar.uitest.MainActivity");
        Activity activity = (Activity) activityClass.getDeclaredConstructor()
            .newInstance();
        Method onCreate = activityClass.getDeclaredMethod("onCreate", Bundle.class);
        onCreate.setAccessible(true);
        onCreate.invoke(activity, new Bundle());

        Class<?> ids = Class.forName("com.muplar.uitest.R$id");
        View action = activity.findViewById(ids.getField("action").getInt(null));
        View status = activity.findViewById(ids.getField("status").getInt(null));
        SwingUtilities.invokeAndWait(new Runnable() {
            @Override public void run() { ((JButton) action.getPeer()).doClick(); }
        });
        String text = ((JLabel) status.getPeer()).getText();
        if (!"Input received".equals(text))
            throw new AssertionError("unexpected status: " + text);
        System.out.println("[UiSmoke] input callback and text update verified");
        activity.finish();
        System.exit(0);
    }
}
