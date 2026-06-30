package com.muplar.launcher;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ApplicationInfo;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.util.Properties;
import java.util.List;

public class LauncherActivity extends Activity {
    private final Properties settings = new Properties();
    private File settingsFile;
    private boolean packageListenerRegistered;
    private String currentScreen = "launcher";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        System.out.println("[LauncherActivity] onCreate called");
        PackageManager pm = getPackageManager();
        System.out.println("[LauncherActivity] package manager resolved: "
            + pm.getClass().getName());
        if (!packageListenerRegistered) {
            packageListenerRegistered = true;
            pm.registerPackageChangeListener(new Runnable() {
                @Override public void run() {
                    runOnUiThread(new Runnable() {
                        @Override public void run() {
                            if ("launcher".equals(currentScreen)) showLauncher();
                        }
                    });
                }
            });
        }

        File stateDirectory = new File(System.getProperty(
            "muplar.prefix.state.dir", System.getProperty("java.io.tmpdir")));
        if (!stateDirectory.isDirectory() && !stateDirectory.mkdirs()) {
            System.err.println("[LauncherActivity] cannot create state directory: "
                + stateDirectory);
        }
        settingsFile = new File(stateDirectory, "launcher.properties");
        if (settingsFile.isFile()) {
            try (FileInputStream input = new FileInputStream(settingsFile)) {
                settings.load(input);
            } catch (Exception error) {
                System.err.println("[LauncherActivity] settings read failed: "
                    + error);
            }
        }
        if ("settings".equals(settings.getProperty("screen"))) showSettings();
        else showLauncher();
    }

    private void showLauncher() {
        currentScreen = "launcher";
        settings.setProperty("screen", currentScreen);
        saveSettings();
        setTitle("Muplar Android Launcher");
        LinearLayout content = verticalLayout();
        TextView heading = new TextView(this);
        heading.setText("Android apps");
        content.addView(heading);

        TextView status = new TextView(this);
        final PackageManager packageManager = getPackageManager();
        List<ApplicationInfo> applications =
            packageManager.getInstalledApplications(0);
        int visibleApps = 0;
        for (ApplicationInfo app : applications) {
            if (!"com.muplar.launcher".equals(app.packageName)) visibleApps++;
        }
        System.out.println("[LauncherActivity] discovered " + visibleApps +
            " installed app(s)");
        status.setText(visibleApps == 1 ? "1 app installed" :
            visibleApps + " apps installed");
        content.addView(status);

        for (final ApplicationInfo app : applications) {
            if ("com.muplar.launcher".equals(app.packageName)) continue;
            Button launch = new Button(this);
            launch.setText(app.loadLabel(packageManager));
            launch.setIconPath(app.iconPath);
            launch.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View view) {
                    Intent intent = packageManager.getLaunchIntentForPackage(
                        app.packageName);
                    if (intent != null) {
                        settings.setProperty("lastLaunchedPackage",
                            app.packageName);
                        saveSettings();
                        startActivity(intent);
                    }
                }
            });
            content.addView(launch);
        }

        Button openSettings = new Button(this);
        openSettings.setText("Open Settings");
        openSettings.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) { showSettings(); }
        });
        content.addView(openSettings);
        setContentView(content);
    }

    private void showSettings() {
        currentScreen = "settings";
        settings.setProperty("screen", currentScreen);
        saveSettings();
        setTitle("Android Settings");
        LinearLayout content = verticalLayout();
        TextView heading = new TextView(this);
        heading.setText("Compatibility");
        content.addView(heading);

        final CheckBox compatibility = new CheckBox(this);
        compatibility.setText("Enable compatibility UI");
        compatibility.setChecked(Boolean.parseBoolean(
            settings.getProperty("compatibilityUi", "true")));
        content.addView(compatibility);

        final CheckBox graphics = new CheckBox(this);
        graphics.setText("Allow host graphics acceleration");
        graphics.setChecked(Boolean.parseBoolean(
            settings.getProperty("hostGraphics", "true")));
        content.addView(graphics);

        Button save = new Button(this);
        save.setText("Save");
        save.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) {
                settings.setProperty("compatibilityUi",
                    Boolean.toString(compatibility.isChecked()));
                settings.setProperty("hostGraphics",
                    Boolean.toString(graphics.isChecked()));
                saveSettings();
                showLauncher();
            }
        });
        content.addView(save);

        Button back = new Button(this);
        back.setText("Back");
        back.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) { showLauncher(); }
        });
        content.addView(back);
        setContentView(content);
    }

    private LinearLayout verticalLayout() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        return layout;
    }

    private void saveSettings() {
        if (settingsFile == null) return;
        try (FileOutputStream output = new FileOutputStream(settingsFile)) {
            settings.store(output, "Muplar Android settings");
        } catch (Exception error) {
            System.err.println("[LauncherActivity] settings write failed: " +
                error);
        }
    }
}
