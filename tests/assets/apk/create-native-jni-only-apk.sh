#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

SYSROOT_TMP="$ROOT_DIR/build/sysroot/data/local/tmp"
APK_ROOT="$ROOT_DIR/build/apk/nativejnionlytest-root-$$"
APK_OUT="$SYSROOT_TMP/nativejnionlytest.apk"

mkdir -p "$APK_ROOT/lib/arm64-v8a" "$SYSROOT_TMP"

cp "$SYSROOT_TMP/libjnionlyapktest.so" \
   "$APK_ROOT/lib/arm64-v8a/libjnionlyapktest.so"

cat > "$APK_ROOT/AndroidManifest.xml" <<'EOF'
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.example.muplar.jnionly">
    <application android:hasCode="false" />
</manifest>
EOF

(cd "$APK_ROOT" && zip -qr "$APK_OUT" AndroidManifest.xml lib)
echo "[apk] Built: $APK_OUT"
file "$APK_OUT"
