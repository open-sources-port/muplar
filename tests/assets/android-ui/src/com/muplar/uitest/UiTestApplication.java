package com.muplar.uitest;

import android.app.Application;

public class UiTestApplication extends Application {
    private static boolean created;
    @Override public void onCreate() {
        super.onCreate();
        created = true;
        System.out.println("[UiTest] Application onCreate");
    }
    public static boolean isCreated() { return created; }
}
