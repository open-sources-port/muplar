#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

SYSROOT_TMP="$ROOT_DIR/build/sysroot/data/local/tmp"
APK_ROOT="$ROOT_DIR/build/apk/javalauncher-root-$$"
APK_OUT="$SYSROOT_TMP/javalauncher.apk"

mkdir -p "$APK_ROOT" "$SYSROOT_TMP"

cat > "$APK_ROOT/AndroidManifest.xml" <<'EOF'
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.example.muplar.launcher">
    <application
        android:hasCode="true"
        android:label="Muplar Java Launcher">
        <activity
            android:name=".LauncherActivity"
            android:exported="true">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
EOF

BOOTSTRAP_JAR="$SYSROOT_TMP/muplar/art/muplar-art-bootstrap.jar"
if [ ! -f "$BOOTSTRAP_JAR" ]; then
    "$ROOT_DIR/tools/build-art-bootstrap-jar.sh" --sysroot "$ROOT_DIR/build/sysroot"
fi

SRC_DIR="$APK_ROOT/java-src"
CLASSES_DIR="$APK_ROOT/classes"
DEX_DIR="$APK_ROOT/dex"
mkdir -p "$SRC_DIR/com/example/muplar/launcher" "$CLASSES_DIR" "$DEX_DIR"

cat > "$SRC_DIR/com/example/muplar/launcher/LauncherActivity.java" <<'EOF'
package com.example.muplar.launcher;

import android.app.Activity;
import android.os.Bundle;
import android.content.pm.PackageManager;
import android.content.Intent;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.util.Properties;

public class LauncherActivity extends Activity {
    private final Properties settings = new Properties();
    private File settingsFile;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        System.out.println("[LauncherActivity] onCreate called");
        PackageManager pm = getPackageManager();
        System.out.println("[LauncherActivity] package manager resolved: " + pm.getClass().getName());
        
        Intent intent = new Intent();
        intent.setClassName("com.example.muplar.tiny", "com.example.muplar.tiny.TinyActivity");
        System.out.println("[LauncherActivity] Launching app: " + intent.getComponentPackage());

        settingsFile = new File(System.getProperty("java.io.tmpdir"),
            "muplar-launcher.properties");
        if (settingsFile.isFile()) {
            try (FileInputStream input = new FileInputStream(settingsFile)) {
                settings.load(input);
            } catch (Exception error) {
                System.err.println("[LauncherActivity] settings read failed: " + error);
            }
        }
        showLauncher();
    }

    private void showLauncher() {
        setTitle("Muplar Android Launcher");
        LinearLayout content = verticalLayout();
        TextView heading = new TextView(this);
        heading.setText("Android apps");
        content.addView(heading);

        TextView status = new TextView(this);
        status.setText("Runtime ready");
        content.addView(status);

        Button openSettings = new Button(this);
        openSettings.setText("Open Settings");
        openSettings.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) { showSettings(); }
        });
        content.addView(openSettings);
        setContentView(content);
    }

    private void showSettings() {
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
                try (FileOutputStream output = new FileOutputStream(settingsFile)) {
                    settings.store(output, "Muplar Android settings");
                } catch (Exception error) {
                    System.err.println("[LauncherActivity] settings write failed: " + error);
                }
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
}
EOF

if command -v javac >/dev/null 2>&1 &&
   javac --help 2>&1 | grep -q -- '--release'; then
    javac -Xlint:-options --release 8 -cp "$BOOTSTRAP_JAR" -d "$CLASSES_DIR" \
        "$SRC_DIR/com/example/muplar/launcher/LauncherActivity.java"
else
    javac -Xlint:-options -source 8 -target 8 -cp "$BOOTSTRAP_JAR" -d "$CLASSES_DIR" \
        "$SRC_DIR/com/example/muplar/launcher/LauncherActivity.java"
fi

find_d8() {
    if [ -n "${D8:-}" ] && [ -x "$D8" ]; then
        echo "$D8"
        return 0
    fi
    if command -v d8 >/dev/null 2>&1; then
        command -v d8
        return 0
    fi
    local sdk
    for sdk in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}" "$HOME/Library/Android/sdk"; do
        if [ -n "$sdk" ] && [ -d "$sdk/build-tools" ]; then
            find "$sdk/build-tools" -type f -name d8 -perm +111 2>/dev/null |
                sort |
                tail -1
            return 0
        fi
    done
    return 1
}

D8_BIN="$(find_d8 || true)"
if [ -n "$D8_BIN" ]; then
    "$D8_BIN" --min-api 23 --output "$DEX_DIR" \
        --classpath "$BOOTSTRAP_JAR" \
        "$CLASSES_DIR/com/example/muplar/launcher/LauncherActivity.class"
    cp "$DEX_DIR/classes.dex" "$APK_ROOT/classes.dex"
    echo "[apk] DEX: real via $D8_BIN"
else
    printf 'muplar-java-launcher-placeholder-dex\n' > "$APK_ROOT/classes.dex"
    echo "[apk] DEX: placeholder"
fi

CLASSES_JAR="${APK_OUT%.apk}-classes.jar"
(cd "$CLASSES_DIR" && jar cf "$CLASSES_JAR" .)
echo "[apk] Classes JAR: $CLASSES_JAR"

(cd "$APK_ROOT" && zip -qr "$APK_OUT" AndroidManifest.xml classes.dex)
echo "[apk] Built: $APK_OUT"
file "$APK_OUT"
