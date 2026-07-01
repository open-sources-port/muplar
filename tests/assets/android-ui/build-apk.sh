#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
OUT_DIR="$ROOT_DIR/build/android-ui-test"
APK_OUT="$OUT_DIR/muplar-ui-test.apk"
CLASSES_JAR="$OUT_DIR/muplar-ui-test-classes.jar"
BOOTSTRAP_JAR="$ROOT_DIR/build/sysroot/data/local/tmp/muplar/art/muplar-art-bootstrap.jar"
SDK_ROOT="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-/opt/android/sdk}}"
AAPT2="$(find "$SDK_ROOT/build-tools" -type f -name aapt2 -perm +111 | sort | tail -1)"
D8="$(find "$SDK_ROOT/build-tools" -type f -name d8 -perm +111 | sort | tail -1)"
ANDROID_JAR="$(find "$SDK_ROOT/platforms" -type f -name android.jar | sort | tail -1)"
JAVAC="${JAVAC:-$ROOT_DIR/third_party/jdk-bin/macos-aarch64/bin/javac}"
JAR="${JAR:-$ROOT_DIR/third_party/jdk-bin/macos-aarch64/bin/jar}"
export JAVA_HOME="${JAVA_HOME:-$ROOT_DIR/third_party/jdk-bin/macos-aarch64}"

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR/classes" "$OUT_DIR/dex" "$OUT_DIR/generated"
"$AAPT2" compile --dir "$SCRIPT_DIR/res" -o "$OUT_DIR/resources.zip"
"$AAPT2" link -I "$ANDROID_JAR" --manifest "$SCRIPT_DIR/AndroidManifest.xml" \
    --java "$OUT_DIR/generated" -o "$APK_OUT" "$OUT_DIR/resources.zip"
"$JAVAC" -Xlint:-options --release 8 -cp "$BOOTSTRAP_JAR:$ANDROID_JAR" \
    -d "$OUT_DIR/classes" \
    "$SCRIPT_DIR/src/com/muplar/uitest/UiTestApplication.java" \
    "$SCRIPT_DIR/src/com/muplar/uitest/MainActivity.java" \
    "$OUT_DIR/generated/com/muplar/uitest/R.java"
find "$OUT_DIR/classes" -type f -name '*.class' -print0 |
    xargs -0 "$D8" --min-api 23 --output "$OUT_DIR/dex" \
        --lib "$ANDROID_JAR" --classpath "$BOOTSTRAP_JAR"
cp "$OUT_DIR/dex/classes.dex" "$OUT_DIR/classes.dex"
(cd "$OUT_DIR" && zip -q "$APK_OUT" classes.dex)
(cd "$OUT_DIR/classes" && "$JAR" cf "$CLASSES_JAR" .)
echo "$APK_OUT"
