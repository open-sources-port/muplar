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

public class LauncherActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        System.out.println("[LauncherActivity] onCreate called");
        PackageManager pm = getPackageManager();
        System.out.println("[LauncherActivity] package manager resolved: " + pm.getClass().getName());
        
        Intent intent = new Intent();
        intent.setClassName("com.example.muplar.tiny", "com.example.muplar.tiny.TinyActivity");
        System.out.println("[LauncherActivity] Launching app: " + intent.getComponentPackage());
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
