#!/bin/sh
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

SYSROOT="$ROOT_DIR/build/sysroot"
OUT=""

usage() {
    echo "Usage: $0 [--sysroot PATH] [--output PATH]"
    echo
    echo "Builds the Muplar Java bootstrap jar used by app_process64."
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --sysroot)
            if [ "$#" -lt 2 ]; then
                echo "--sysroot requires a path" >&2
                exit 2
            fi
            SYSROOT="$2"
            shift 2
            ;;
        --output)
            if [ "$#" -lt 2 ]; then
                echo "--output requires a path" >&2
                exit 2
            fi
            OUT="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [ -z "$OUT" ]; then
    OUT="$SYSROOT/data/local/tmp/muplar/art/muplar-art-bootstrap.jar"
fi

case "$SYSROOT" in
    /*) ;;
    *) SYSROOT="$ROOT_DIR/$SYSROOT" ;;
esac

case "$OUT" in
    /*) ;;
    *) OUT="$ROOT_DIR/$OUT" ;;
esac

SRC_DIR="$ROOT_DIR/platform/android-aarch64/java-bootstrap"
BUILD_DIR="$ROOT_DIR/build/java/art-bootstrap"
CLASSES_DIR="$BUILD_DIR/classes"
DEX_DIR="$BUILD_DIR/dex"

JAVAC_BIN="${JAVAC:-javac}"
JAR_BIN="${JAR:-jar}"
D8_BIN="${D8:-}"
ANDROID_JAR="${ANDROID_JAR:-}"

if [ -f "$JAVAC_BIN" ] && [ ! -x "$JAVAC_BIN" ]; then
    chmod 755 "$JAVAC_BIN" 2>/dev/null || true
fi

if [ -f "$JAR_BIN" ] && [ ! -x "$JAR_BIN" ]; then
    chmod 755 "$JAR_BIN" 2>/dev/null || true
fi

if ! command -v "$JAVAC_BIN" >/dev/null 2>&1; then
    if [ -n "${JAVA_HOME:-}" ] && [ -x "${JAVA_HOME}/bin/javac" ]; then
        JAVAC_BIN="${JAVA_HOME}/bin/javac"
    elif command -v javac >/dev/null 2>&1; then
        JAVAC_BIN="javac"
    else
        echo "javac not found. Source /etc/share-environment.sh or set JAVAC." >&2
        exit 1
    fi
fi

if ! command -v "$JAR_BIN" >/dev/null 2>&1; then
    if [ -n "${JAVA_HOME:-}" ] && [ -x "${JAVA_HOME}/bin/jar" ]; then
        JAR_BIN="${JAVA_HOME}/bin/jar"
    elif command -v jar >/dev/null 2>&1; then
        JAR_BIN="jar"
    else
        echo "jar not found. Source /etc/share-environment.sh or set JAR." >&2
        exit 1
    fi
fi

if [ -z "$D8_BIN" ]; then
    for base in \
        "${ANDROID_HOME:-}/build-tools" \
        "${ANDROID_SDK_ROOT:-}/build-tools" \
        /opt/android/sdk/build-tools \
        "$HOME/Library/Android/sdk/build-tools"; do
        if [ -d "$base" ]; then
            candidate="$(find "$base" -mindepth 2 -maxdepth 2 -type f -name d8 2>/dev/null | sort | tail -n 1)"
            if [ -n "$candidate" ] && [ -x "$candidate" ]; then
                D8_BIN="$candidate"
            fi
        fi
    done
fi

if [ -z "$D8_BIN" ] || [ ! -x "$D8_BIN" ]; then
    echo "d8 not found. Install Android build-tools or set D8." >&2
    exit 1
fi

if [ -z "$ANDROID_JAR" ]; then
    for base in \
        "${ANDROID_HOME:-}/platforms" \
        "${ANDROID_SDK_ROOT:-}/platforms" \
        /opt/android/sdk/platforms \
        "$HOME/Library/Android/sdk/platforms"; do
        if [ -d "$base" ]; then
            candidate="$(find "$base" -mindepth 2 -maxdepth 2 -type f -name android.jar 2>/dev/null | sort | tail -n 1)"
            if [ -n "$candidate" ] && [ -f "$candidate" ]; then
                ANDROID_JAR="$candidate"
            fi
        fi
    done
fi

if [ -z "$ANDROID_JAR" ] || [ ! -f "$ANDROID_JAR" ]; then
    echo "android.jar not found. Install Android SDK platforms or set ANDROID_JAR." >&2
    exit 1
fi

rm -rf "$CLASSES_DIR" "$DEX_DIR"
mkdir -p "$CLASSES_DIR" "$DEX_DIR" "$(dirname "$OUT")"

cat > "$BUILD_DIR/sources.txt" <<EOF
$SRC_DIR/com/muplar/runtime/ArtApkMain.java
$SRC_DIR/com/muplar/runtime/FrameworkProcessSession.java
$SRC_DIR/com/muplar/runtime/FrameworkServiceClient.java
$SRC_DIR/com/muplar/runtime/MuplarApplication.java
$SRC_DIR/com/muplar/runtime/MuplarContentResolver.java
$SRC_DIR/com/muplar/runtime/MuplarContext.java
$SRC_DIR/com/muplar/runtime/MuplarGraphics.java
$SRC_DIR/com/muplar/runtime/MuplarLayoutInflater.java
$SRC_DIR/com/muplar/runtime/MuplarPackageManager.java
$SRC_DIR/com/muplar/runtime/MuplarServices.java
$SRC_DIR/com/muplar/runtime/MuplarSharedPreferences.java
$SRC_DIR/com/muplar/runtime/MuplarVsyncScheduler.java
$SRC_DIR/com/muplar/runtime/MuplarWindow.java
$SRC_DIR/android/app/admin/IDevicePolicyManager.java
$SRC_DIR/android/app/admin/ParcelableResource.java
$SRC_DIR/android/app/IApplicationThread.java
$SRC_DIR/android/app/StatsManager.java
$SRC_DIR/android/content/IContentProvider.java
$SRC_DIR/android/hardware/MuplarSensorManager.java
$SRC_DIR/android/os/ICancellationSignal.java
$SRC_DIR/android/provider/DeviceConfig.java
$SRC_DIR/android/util/StatsEvent.java
$SRC_DIR/android/util/StatsLog.java
$SRC_DIR/android/view/IRecentsAnimationController.java
$SRC_DIR/android/view/IRecentsAnimationRunner.java
$SRC_DIR/android/view/RemoteAnimationTarget.java
$SRC_DIR/android/widget/EditText.java
$SRC_DIR/android/window/TaskSnapshot.java
$SRC_DIR/android/view/autofill/AutofillManager.java
$SRC_DIR/android/content/ContentCaptureOptions.java
$SRC_DIR/android/content/AutofillOptions.java
EOF

if "$JAVAC_BIN" --help 2>&1 | grep -q -- '--release'; then
    "$JAVAC_BIN" -Xlint:-options --release 8 -classpath "$ANDROID_JAR" \
        -sourcepath "$SRC_DIR" \
        -d "$CLASSES_DIR" @"$BUILD_DIR/sources.txt"
else
    "$JAVAC_BIN" -Xlint:-options -source 8 -target 8 -classpath "$ANDROID_JAR" \
        -sourcepath "$SRC_DIR" -d "$CLASSES_DIR" @"$BUILD_DIR/sources.txt"
fi

find "$CLASSES_DIR" -name '*.class' -print > "$BUILD_DIR/class-files.txt"
if [ ! -s "$BUILD_DIR/class-files.txt" ]; then
    echo "no Java class files were generated" >&2
    exit 1
fi
xargs "$D8_BIN" --min-api 35 --output "$DEX_DIR" < "$BUILD_DIR/class-files.txt"

(cd "$DEX_DIR" && "$JAR_BIN" cf "$OUT" classes.dex)

echo "[ART] Built bootstrap jar: $OUT"
file "$OUT"
