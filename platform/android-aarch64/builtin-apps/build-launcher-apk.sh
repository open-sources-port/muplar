#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

SYSROOT_TMP="$ROOT_DIR/build/sysroot/data/local/tmp"
APK_ROOT="$ROOT_DIR/build/apk/javalauncher-root-$$"
APK_OUT="$SYSROOT_TMP/javalauncher.apk"
LAUNCHER_DIR="$SCRIPT_DIR/launcher"
LAUNCHER_SOURCE="$LAUNCHER_DIR/src/com/muplar/launcher/LauncherActivity.java"
JAVAC_BIN="${JAVAC:-javac}"
JAR_BIN="${JAR:-jar}"

mkdir -p "$APK_ROOT" "$SYSROOT_TMP"
cp "$LAUNCHER_DIR/AndroidManifest.xml" "$APK_ROOT/AndroidManifest.xml"

BOOTSTRAP_JAR="$SYSROOT_TMP/muplar/art/muplar-art-bootstrap.jar"
if [ ! -f "$BOOTSTRAP_JAR" ]; then
    "$ROOT_DIR/tools/build-art-bootstrap-jar.sh" --sysroot "$ROOT_DIR/build/sysroot"
fi

CLASSES_DIR="$APK_ROOT/classes"
DEX_DIR="$APK_ROOT/dex"
mkdir -p "$CLASSES_DIR" "$DEX_DIR"

if "$JAVAC_BIN" --help 2>&1 | grep -q -- '--release'; then
    "$JAVAC_BIN" -Xlint:-options --release 8 -cp "$BOOTSTRAP_JAR" -d "$CLASSES_DIR" \
        "$LAUNCHER_SOURCE"
else
    "$JAVAC_BIN" -Xlint:-options -source 8 -target 8 -cp "$BOOTSTRAP_JAR" -d "$CLASSES_DIR" \
        "$LAUNCHER_SOURCE"
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
    for sdk in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}" \
        "$HOME/Library/Android/sdk" /opt/android/sdk; do
        if [ -n "$sdk" ] && [ -d "$sdk/build-tools" ]; then
            find "$sdk/build-tools" -type f -name d8 -perm +111 2>/dev/null |
                sort |
                tail -1
            return 0
        fi
    done
    return 1
}

find_aapt2() {
    if [ -n "${AAPT2:-}" ] && [ -x "$AAPT2" ]; then
        echo "$AAPT2"
        return 0
    fi
    if command -v aapt2 >/dev/null 2>&1; then
        command -v aapt2
        return 0
    fi
    local sdk
    for sdk in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}" \
        "$HOME/Library/Android/sdk" /opt/android/sdk; do
        if [ -n "$sdk" ] && [ -d "$sdk/build-tools" ]; then
            find "$sdk/build-tools" -type f -name aapt2 -perm +111 2>/dev/null |
                sort |
                tail -1
            return 0
        fi
    done
    return 1
}

find_android_jar() {
    local sdk
    for sdk in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}" \
        "$HOME/Library/Android/sdk" /opt/android/sdk; do
        if [ -n "$sdk" ] && [ -d "$sdk/platforms" ]; then
            find "$sdk/platforms" -type f -name android.jar 2>/dev/null |
                sort |
                tail -1
            return 0
        fi
    done
    return 1
}

D8_BIN="$(find_d8 || true)"
AAPT2_BIN="$(find_aapt2 || true)"
ANDROID_JAR="$(find_android_jar || true)"
if [ -n "$D8_BIN" ] && [ -n "$AAPT2_BIN" ] && [ -n "$ANDROID_JAR" ]; then
    CLASS_FILES="$(find "$CLASSES_DIR" -type f -name '*.class' -print)"
    if [ -z "$CLASS_FILES" ]; then
        echo "No compiled launcher classes found under $CLASSES_DIR." >&2
        exit 1
    fi
    find "$CLASSES_DIR" -type f -name '*.class' -print0 |
        xargs -0 "$D8_BIN" --min-api 23 --output "$DEX_DIR" \
            --lib "$ANDROID_JAR" \
            --classpath "$BOOTSTRAP_JAR"
    cp "$DEX_DIR/classes.dex" "$APK_ROOT/classes.dex"
    echo "[apk] DEX: real via $D8_BIN"
else
    echo "Android SDK build-tools/platform not found; set D8, AAPT2, and ANDROID_HOME." >&2
    exit 1
fi

CLASSES_JAR="${APK_OUT%.apk}-classes.jar"
(cd "$CLASSES_DIR" && "$JAR_BIN" cf "$CLASSES_JAR" .)
echo "[apk] Classes JAR: $CLASSES_JAR"

COMPILED_RES="$APK_ROOT/compiled-res.zip"
"$AAPT2_BIN" compile --dir "$LAUNCHER_DIR/res" -o "$COMPILED_RES"
"$AAPT2_BIN" link \
    -I "$ANDROID_JAR" \
    --manifest "$LAUNCHER_DIR/AndroidManifest.xml" \
    --min-sdk-version 23 \
    --target-sdk-version 35 \
    -o "$APK_OUT" \
    "$COMPILED_RES"
(cd "$APK_ROOT" && zip -q "$APK_OUT" classes.dex)
echo "[apk] Built: $APK_OUT"
file "$APK_OUT"
