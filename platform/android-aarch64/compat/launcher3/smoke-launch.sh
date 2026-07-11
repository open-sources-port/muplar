#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
FIXTURE="$ROOT_DIR/build/launcher3/fixture/Launcher3.apk"
LOG="${TMPDIR:-/tmp}/muplar-launcher3-smoke.log"

if [[ ! -f "$FIXTURE" ]]; then
    echo "Launcher3 fixture is missing; run fetch-official-apk.sh first" >&2
    exit 1
fi

mkdir -p "$(dirname "$LOG")"
: > "$LOG"
"$ROOT_DIR/build/bin/mup" --prefix android-arm64 --apk "$FIXTURE" \
    >"$LOG" 2>&1 &
PID=$!
cleanup() {
    pkill -TERM -P "$PID" 2>/dev/null || true
    kill "$PID" 2>/dev/null || true
    wait "$PID" 2>/dev/null || true
}
trap cleanup EXIT

for _ in {1..300}; do
    if grep -q '\[Muplar/ART\] ArtApkMain started' "$LOG" &&
       grep -q 'onStart/onResume completed successfully' "$LOG" &&
       grep -q 'makeVisible completed successfully' "$LOG" &&
       grep -q 'entering main looper' "$LOG"; then
        echo "Launcher3 guest ART lifecycle smoke passed"
        exit 0
    fi
    if grep -qE 'bootstrap failed|failed to invoke onCreate' "$LOG"; then
        tail -80 "$LOG" >&2
        exit 1
    fi
    kill -0 "$PID" 2>/dev/null || break
    sleep 0.1
done

tail -80 "$LOG" >&2
echo "Launcher3 did not complete guest ART lifecycle" >&2
exit 1
