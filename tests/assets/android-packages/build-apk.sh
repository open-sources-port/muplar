#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
SDK_ROOT="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-/opt/android/sdk}}"
BUILD_TOOLS="${ANDROID_BUILD_TOOLS:-$SDK_ROOT/build-tools/36.1.0}"
ANDROID_JAR="${ANDROID_JAR:-}"
if [ -z "$ANDROID_JAR" ]; then
    for candidate in "$SDK_ROOT"/platforms/*/android.jar; do
        if [ -f "$candidate" ]; then
            ANDROID_JAR="$candidate"
        fi
    done
fi
if [ ! -f "$ANDROID_JAR" ]; then
    echo "Android SDK platform jar missing; set ANDROID_JAR." >&2
    exit 1
fi
OUT="$ROOT_DIR/build/android-package-test"
mkdir -p "$OUT/classes" "$OUT/dex"
"$BUILD_TOOLS/aapt2" link -I "$ANDROID_JAR" \
    --manifest "$SCRIPT_DIR/AndroidManifest.xml" -o "$OUT/packages.apk"
javac --release 8 -cp "$ANDROID_JAR" -d "$OUT/classes" \
    "$SCRIPT_DIR/src/com/muplar/packagesmoke/MainActivity.java"
"$BUILD_TOOLS/d8" --min-api 23 --lib "$ANDROID_JAR" --output "$OUT/dex" \
    "$OUT/classes/com/muplar/packagesmoke/MainActivity.class"
cp "$OUT/dex/classes.dex" "$OUT/classes.dex"
(cd "$OUT" && zip -q packages.apk classes.dex)
if [ ! -f "$OUT/test-signing.jks" ]; then
    keytool -genkeypair -keystore "$OUT/test-signing.jks" -storepass androidtest \
        -keypass androidtest -alias package-smoke -keyalg RSA -keysize 2048 \
        -validity 3650 -dname "CN=Muplar package smoke test" >/dev/null 2>&1
fi
"$BUILD_TOOLS/apksigner" sign --ks "$OUT/test-signing.jks" \
    --ks-pass pass:androidtest --key-pass pass:androidtest "$OUT/packages.apk"
"$BUILD_TOOLS/apksigner" verify "$OUT/packages.apk"
echo "$OUT/packages.apk"
