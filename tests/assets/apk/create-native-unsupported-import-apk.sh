#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

SYSROOT_TMP="$ROOT_DIR/build/sysroot/data/local/tmp"
APK_ROOT="$ROOT_DIR/build/apk/nativeunsupportedimporttest-root-$$"
APK_OUT="$SYSROOT_TMP/nativeunsupportedimporttest.apk"

mkdir -p "$APK_ROOT/lib/arm64-v8a" "$SYSROOT_TMP"

cp "$SYSROOT_TMP/libunsupportedimporttest.so" \
   "$APK_ROOT/lib/arm64-v8a/libunsupportedimporttest.so"

cat > "$APK_ROOT/AndroidManifest.xml" <<'EOF'
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.example.muplar.unsupportedimport">
    <application android:hasCode="false">
        <activity
            android:name="android.app.NativeActivity"
            android:exported="true">
            <meta-data
                android:name="android.app.lib_name"
                android:value="unsupportedimporttest" />
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
