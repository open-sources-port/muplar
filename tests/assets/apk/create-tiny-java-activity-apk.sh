#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

SYSROOT_TMP="$ROOT_DIR/build/sysroot/data/local/tmp"
APK_ROOT="$ROOT_DIR/build/apk/tinyjavaactivity-root-$$"
APK_OUT="$SYSROOT_TMP/tinyjavaactivity.apk"
REQUIRE_REAL_DEX=false

while [ "$#" -gt 0 ]; do
    case "$1" in
        --require-real-dex)
            REQUIRE_REAL_DEX=true
            shift
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

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

mkdir -p "$APK_ROOT" "$SYSROOT_TMP"

cat > "$APK_ROOT/AndroidManifest.xml" <<'EOF'
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.example.muplar.tiny">
    <application
        android:hasCode="true"
        android:label="Muplar Tiny Activity">
        <activity
            android:name=".TinyActivity"
            android:exported="true">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
EOF

D8_BIN="$(find_d8 || true)"
if [ -n "$D8_BIN" ]; then
    SRC_DIR="$APK_ROOT/java-src"
    CLASSES_DIR="$APK_ROOT/classes"
    DEX_DIR="$APK_ROOT/dex"
    mkdir -p "$SRC_DIR/com/example/muplar/tiny" "$CLASSES_DIR" "$DEX_DIR"
    cat > "$SRC_DIR/com/example/muplar/tiny/TinyActivity.java" <<'EOF'
package com.example.muplar.tiny;

public final class TinyActivity {
    private TinyActivity() {
    }

    public static String marker() {
        return "muplar-tiny-java-activity";
    }
}
EOF

    if command -v javac >/dev/null 2>&1 &&
       javac --help 2>&1 | grep -q -- '--release'; then
        javac -Xlint:-options --release 8 -d "$CLASSES_DIR" \
            "$SRC_DIR/com/example/muplar/tiny/TinyActivity.java"
    else
        javac -Xlint:-options -source 8 -target 8 -d "$CLASSES_DIR" \
            "$SRC_DIR/com/example/muplar/tiny/TinyActivity.java"
    fi
    "$D8_BIN" --min-api 23 --output "$DEX_DIR" \
        "$CLASSES_DIR/com/example/muplar/tiny/TinyActivity.class"
    cp "$DEX_DIR/classes.dex" "$APK_ROOT/classes.dex"
    echo "[apk] DEX: real via $D8_BIN"

    # Also produce a plain classes JAR for the host JVM launcher (URLClassLoader
    # cannot read DEX bytecode; it needs standard .class files in a JAR).
    CLASSES_JAR="${APK_OUT%.apk}-classes.jar"
    (cd "$CLASSES_DIR" && jar cf "$CLASSES_JAR" .)
    echo "[apk] Classes JAR: $CLASSES_JAR"
elif [ "$REQUIRE_REAL_DEX" = true ]; then
    echo "d8 not found; cannot build real tiny Java Activity DEX." >&2
    echo "Set D8=/path/to/d8 or ANDROID_HOME/ANDROID_SDK_ROOT to an Android SDK." >&2
    exit 1
else
    printf 'muplar-tiny-java-activity-placeholder-dex\n' > "$APK_ROOT/classes.dex"
    echo "[apk] DEX: placeholder; install d8 for real class-loader execution"
fi

(cd "$APK_ROOT" && zip -qr "$APK_OUT" AndroidManifest.xml classes.dex)
echo "[apk] Built: $APK_OUT"
file "$APK_OUT"
