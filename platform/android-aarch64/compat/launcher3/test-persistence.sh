#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
JDK="$ROOT_DIR/third_party/jdk-bin/macos-aarch64/bin"
BOOTSTRAP="$ROOT_DIR/build/sysroot/data/local/tmp/muplar/art/muplar-art-bootstrap.jar"
CLASSES="$ROOT_DIR/build/tests/launcher3-persistence-classes"
STATE="$ROOT_DIR/build/tests/launcher3-persistence-state-$$"

if [[ ! -x "$JDK/javac" || ! -x "$JDK/java" ]]; then
    echo "Bundled JDK is missing; run tools/setup-jdk.sh" >&2
    exit 1
fi

JAVAC="$JDK/javac" JAR="$JDK/jar" \
    "$ROOT_DIR/tools/build-art-bootstrap-jar.sh" --sysroot "$ROOT_DIR/build/sysroot"
mkdir -p "$CLASSES" "$STATE"
"$JDK/javac" -Xlint:-options --release 8 -cp "$BOOTSTRAP" \
    -d "$CLASSES" "$ROOT_DIR/tests/android/SQLitePersistenceSmoke.java"
"$JDK/java" -cp "$BOOTSTRAP:$CLASSES" \
    android.database.sqlite.SQLitePersistenceSmoke "$STATE/launcher.db"
