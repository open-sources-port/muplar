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
MUPLAR_HOST_TEST_SWIPE_UP=1 MUPLAR_HOST_TEST_CLICK_FIRST_APP=1 \
MUPLAR_HOST_TEST_SCREENSHOT="${MUPLAR_LAUNCHER3_SCREENSHOT:-}" \
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
    if grep -q 'onStart/onResume completed successfully' "$LOG" &&
       grep -q 'loadAllApps' "$LOG" &&
       grep -qE 'materialized RecyclerView items=[12]' "$LOG" &&
       grep -q 'onStateTransitionEnd - state: AllApps' "$LOG" &&
       grep -q 'test first app click dispatched=true' "$LOG" &&
       grep -q '\[IntentDispatcher\] launched ' "$LOG"; then
        if [[ -n "${MUPLAR_LAUNCHER3_SCREENSHOT:-}" &&
              ! -s "$MUPLAR_LAUNCHER3_SCREENSHOT" ]]; then
            continue
        fi
        echo "Launcher3 lifecycle, All Apps, and application launch smoke passed"
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
echo "Launcher3 did not complete lifecycle, All Apps, and application launch" >&2
exit 1
