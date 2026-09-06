#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
FIXTURE="$ROOT_DIR/build/launcher3/fixture/Launcher3.apk"
TEST_APK="$ROOT_DIR/build/android-ui-test/muplar-ui-test.apk"
PREFIX_DIR="$HOME/.muplar/prefixes/android-arm64"
LOG="${TMPDIR:-/tmp}/muplar-touch-interaction-test.log"

mkdir -p "$(dirname "$LOG")"
: > "$LOG"

# Sync latest builds into prefix
"$ROOT_DIR/tools/build-android-art-shim.sh" >/dev/null
"$ROOT_DIR/tools/build-art-bootstrap-jar.sh" >/dev/null

echo "Starting Launcher3 with test APK..."
export MUPLAR_SERVICE_SOCKET="$PREFIX_DIR/run/muplard.sock"
export MUPLAR_ANDROID_SOFTWARE_FRAME_PATH="/data/local/tmp/muplar/frames/software-frame.mhr"
"$ROOT_DIR/build/bin/mup" --prefix android-arm64 --apk "$FIXTURE" \
    >"$LOG" 2>&1 &
PID=$!
cleanup() {
    set +e
    pkill -TERM -P "$PID" 2>/dev/null
    kill "$PID" 2>/dev/null
    wait "$PID" 2>/dev/null
    exit 0
}
trap cleanup EXIT

echo "Waiting for main looper..."
for _ in {1..300}; do
    if grep -q 'entering main looper' "$LOG"; then
        break
    fi
    kill -0 "$PID" 2>/dev/null || break
    sleep 0.1
done

SOCK_PATH="$PREFIX_DIR/run/muplard.sock"

echo "Focusing MainActivity..."
python3 "$SCRIPT_DIR/test-backstack.py" "$SOCK_PATH" focus-tab "$PREFIX_DIR/packages/muplar-ui-test.apk"

for _ in {1..200}; do
    if grep -q 'button screen coords' "$LOG"; then
        break
    fi
    sleep 0.1
done

echo "MainActivity running. Dispatching touch tap..."
# Dispatch ACTION_DOWN and ACTION_UP via opcode 27
python3 -c "
import socket, struct, time, sys

def send_input(action, x, y):
    magic = 0x4d555044
    version = 1
    req_id = 1
    # tab \n type=2 \n action \n source=4098 \n device_id=1 \n key_code=0 \n x \n y
    payload = f'com.muplar.uitest\n2\n{action}\n4098\n1\n0\n{x}\n{y}'.encode('utf-8')
    header = struct.pack('<IHHI4xQ', magic, version, 27, len(payload), req_id)
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect('$SOCK_PATH')
    sock.sendall(header + payload)
    resp = sock.recv(1024)
    sock.close()

# Tap directly on first list item (x=200, y=340)
send_input(0, 200.0, 340.0)
time.sleep(0.1)
send_input(1, 200.0, 340.0)
print('Touch tap sent successfully')
"

sleep 0.5

echo "Checking log for touch results and stability..."
if grep -E 'SIGSEGV|exit code: 139|UnsatisfiedLinkError' "$LOG"; then
    echo "ERROR: Crash or unsatisfied link detected!" >&2
    grep -E -C 3 'SIGSEGV|exit code: 139|UnsatisfiedLinkError' "$LOG" >&2
    exit 1
fi

if ! grep -q 'button input received' "$LOG"; then
    echo "ERROR: button input was not received!" >&2
    tail -40 "$LOG" >&2
    exit 1
fi

echo "Log verification:"
grep -E 'button input received|list item clicked|launching SecondActivity|motion trace|dispatchTouchEvent|input apply' "$LOG" | tail -10 || true

echo "SUCCESS: Touch dispatch executed cleanly and button click was received!"
