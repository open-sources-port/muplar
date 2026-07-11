package com.muplar.runtime;

import android.app.Application;
import android.content.Context;
import android.content.res.AssetManager;
import android.content.res.Resources;
import java.io.File;

public final class MuplarApplication extends Application {
    private final Context context;

    public MuplarApplication(Context context) {
        this.context = context;
    }

    @Override
    public Context getApplicationContext() {
        return this;
    }

    @Override
    public AssetManager getAssets() {
        return context.getAssets();
    }

    @Override
    public Resources getResources() {
        return context.getResources();
    }

    @Override
    public Resources.Theme getTheme() {
        return context.getTheme();
    }

    @Override
    public Object getSystemService(String name) {
        return context.getSystemService(name);
    }

    @Override
    public String getSystemServiceName(Class<?> serviceClass) {
        return context.getSystemServiceName(serviceClass);
    }

    @Override
    public File getDatabasePath(String name) {
        return context.getDatabasePath(name);
    }

    @Override
    public File getFileStreamPath(String name) {
        File dir = new File(((MuplarContext)context).getApplicationInfo().dataDir,
            "files");
        if (!dir.exists()) {
            dir.mkdirs();
        }
        return new File(dir, name == null ? "" : name);
    }
}
