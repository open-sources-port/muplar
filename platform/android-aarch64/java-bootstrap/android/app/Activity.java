package android.app;

import android.content.Context;
import android.os.Bundle;
import android.view.View;
import com.muplar.runtime.HostUi;

public class Activity extends Context {
    private String title = "Android";

    public Activity() {}

    protected void onCreate(Bundle savedInstanceState) {
        // stub
    }

    public void setTitle(CharSequence title) {
        this.title = title == null ? "Android" : title.toString();
    }

    public void setContentView(View view) {
        HostUi.showActivity(this, title, view == null ? null : view.getPeer());
    }

    public void finish() {
        HostUi.finishActivity(this);
    }
}
