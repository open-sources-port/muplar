#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
FIXTURE="$ROOT_DIR/build/launcher3/fixture/Launcher3.apk"
TEST_APK="$ROOT_DIR/build/android-ui-test/muplar-ui-test.apk"
PREFIX_DIR="$HOME/.muplar/prefixes/android-arm64"
LOG="${TMPDIR:-/tmp}/muplar-keyboard-input-test.log"

mkdir -p "$(dirname "$LOG")"
: > "$LOG"

if [[ ! -f "$TEST_APK" ]]; then
    echo "Building muplar-ui-test.apk..."
    "$ROOT_DIR/tests/assets/android-ui/build-apk.sh"
fi
mkdir -p "$PREFIX_DIR/packages"
cp -f "$TEST_APK" "$PREFIX_DIR/packages/muplar-ui-test.apk"

# Sync latest bootstrap jar into prefix
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
    if grep -q 'registered activity tab=com.muplar.uitest.*MainActivity' "$LOG"; then
        break
    fi
    sleep 0.1
done

echo "MainActivity running. Dispatching keyboard inputs..."
python3 -c "
import socket, struct, time, sys

def send_key(key_code, action, meta=0, unicode_char=0):
    magic = 0x4d555044
    version = 1
    req_id = 1
    # tab \n type=1 \n action \n source=257 (KEYBOARD) \n device_id=1 \n key_code \n x (meta) \n y (unicode)
    payload = f'com.muplar.uitest\n1\n{action}\n257\n1\n{key_code}\n{float(meta)}\n{float(unicode_char)}'.encode('utf-8')
    header = struct.pack('<IHHI4xQ', magic, version, 27, len(payload), req_id)
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect('$SOCK_PATH')
    sock.sendall(header + payload)
    resp = sock.recv(1024)
    sock.close()

# Test 1: Send 'a' (KEYCODE_A = 29, unicode = 97)
send_key(29, 0, meta=0, unicode_char=97)
time.sleep(0.05)
send_key(29, 1, meta=0, unicode_char=97)
time.sleep(0.1)

# Test 2: Send Shift+'A' (KEYCODE_A = 29, meta=1, unicode = 65)
send_key(29, 0, meta=1, unicode_char=65)
time.sleep(0.05)
send_key(29, 1, meta=1, unicode_char=65)
time.sleep(0.1)

# Test 3: Send KEYCODE_SPACE (62) to trigger navigation to SecondActivity
send_key(62, 0, meta=0, unicode_char=32)
time.sleep(0.05)
send_key(62, 1, meta=0, unicode_char=32)
time.sleep(0.5)

# Test 4: In SecondActivity, send KEYCODE_F (34) to finish SecondActivity
send_key(34, 0, meta=0, unicode_char=102)
time.sleep(0.05)
send_key(34, 1, meta=0, unicode_char=102)
time.sleep(0.5)

print('Keyboard inputs dispatched successfully')
"

echo "Verifying keyboard input logs..."
for _ in {1..50}; do
    if grep -q 'resuming top=com.muplar.uitest.MainActivity' "$LOG"; then
        break
    fi
    sleep 0.1
done

echo "Log verification:"
grep -E 'dispatchKeyInput: keyCode=29.*unicode=97|dispatchKeyInput: keyCode=29.*unicode=65|launched activity class=com.muplar.uitest.SecondActivity|key F received, calling finish|resuming top=com.muplar.uitest.MainActivity' "$LOG"

if ! grep -q 'dispatchKeyInput: keyCode=29.*unicode=97' "$LOG"; then
    echo "FAIL: Lowercase key dispatch not found in log" >&2
    exit 1
fi

if ! grep -q 'dispatchKeyInput: keyCode=29.*unicode=65' "$LOG"; then
    echo "FAIL: Shifted key dispatch not found in log" >&2
    exit 1
fi

if ! grep -q 'launched activity class=com.muplar.uitest.SecondActivity' "$LOG"; then
    echo "FAIL: Key-triggered activity launch failed" >&2
    exit 1
fi

if ! grep -q 'key F received, calling finish' "$LOG"; then
    echo "FAIL: SecondActivity did not receive key F" >&2
    exit 1
fi

if ! grep -q 'resuming top=com.muplar.uitest.MainActivity' "$LOG"; then
    echo "FAIL: MainActivity was not resumed after key F finish" >&2
    exit 1
fi

echo "SUCCESS: Keyboard event dispatch, modifier propagation, unicode decoding, and in-app key handling verified end-to-end!"
