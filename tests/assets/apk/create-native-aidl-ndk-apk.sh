#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

SYSROOT_TMP="$ROOT_DIR/build/sysroot/data/local/tmp"
APK_ROOT="$ROOT_DIR/build/apk/nativeaidlndktest-root-$$"
APK_OUT="$SYSROOT_TMP/nativeaidlndktest.apk"

mkdir -p "$APK_ROOT/lib/arm64-v8a" "$SYSROOT_TMP"

cp "$SYSROOT_TMP/libaidlndktest.so" \
   "$APK_ROOT/lib/arm64-v8a/libaidlndktest.so"
cp "$SYSROOT_TMP/libc++_shared.so" \
   "$APK_ROOT/lib/arm64-v8a/libc++_shared.so"

cat > "$APK_ROOT/AndroidManifest.xml" <<'EOF'
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.example.muplar.aidlndk">
    <application android:hasCode="false">
        <activity
            android:name="android.app.NativeActivity"
            android:exported="true">
            <meta-data
                android:name="android.app.lib_name"
                android:value="aidlndktest" />
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
EOF

(cd "$APK_ROOT" && zip -qr "$APK_OUT" AndroidManifest.xml lib)
echo "[apk] Built: $APK_OUT"
file "$APK_OUT"
