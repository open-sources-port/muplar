#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

SYSROOT_TMP="$ROOT_DIR/build/sysroot/data/local/tmp"
APK_ROOT="$ROOT_DIR/build/apk/javaonlytest-root-$$"
APK_OUT="$SYSROOT_TMP/javaonlytest.apk"

mkdir -p "$APK_ROOT" "$SYSROOT_TMP"

cat > "$APK_ROOT/AndroidManifest.xml" <<'EOF'
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.example.muplar.javaonly">
    <application android:hasCode="true" />
</manifest>
EOF

printf 'muplar-java-placeholder-dex\n' > "$APK_ROOT/classes.dex"

(cd "$APK_ROOT" && zip -qr "$APK_OUT" AndroidManifest.xml classes.dex)
echo "[apk] Built: $APK_OUT"
file "$APK_OUT"
