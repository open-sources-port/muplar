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
OUT="$ROOT_DIR/build/android-subscription-test"
mkdir -p "$OUT/classes" "$OUT/dex"
"$BUILD_TOOLS/aapt2" link -I "$ANDROID_JAR" \
    --manifest "$SCRIPT_DIR/AndroidManifest.xml" -o "$OUT/subscriptions.apk"
javac -Xlint:-options --release 8 -cp "$ANDROID_JAR" -d "$OUT/classes" \
    "$SCRIPT_DIR/src/com/muplar/subscriptionsmoke/MainActivity.java"
"$BUILD_TOOLS/d8" --min-api 23 --lib "$ANDROID_JAR" --output "$OUT/dex" \
    "$OUT/classes/com/muplar/subscriptionsmoke/"*.class
cp "$OUT/dex/classes.dex" "$OUT/classes.dex"
(cd "$OUT" && zip -q subscriptions.apk classes.dex)
echo "$OUT/subscriptions.apk"
