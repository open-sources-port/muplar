#!/bin/zsh
set -euo pipefail

DEST_DIR="${1:-$HOME/.muplar/sysroots/android-arm64/extracted}"
echo "Target extract directory: $DEST_DIR"

# Ensure adb is available
if ! command -v adb >/dev/null 2>&1; then
    echo "Error: adb command not found. Please install Android Platform Tools." >&2
    exit 1
fi

# Check device authorization status
device_status=$(adb devices | grep -E "^[a-zA-Z0-9_-]+\s+" | grep -v "devices" || true)
if [ -z "$device_status" ]; then
    echo "Error: No devices detected by adb. Make sure your device is connected via USB." >&2
    exit 1
fi

if echo "$device_status" | grep -q "unauthorized"; then
    echo "=========================================================="
    echo "WARNING: Your device status is 'unauthorized'."
    echo "Please check your phone's screen and allow USB debugging."
    echo "=========================================================="
    exit 1
fi

echo "Connected device found:"
echo "$device_status"

mkdir -p "$DEST_DIR"

# Helper to adb pull a file if it exists, creating parent dirs locally
pull_file() {
    local remote_path="$1"
    local local_rel_path="${remote_path#/}"
    local local_path="$DEST_DIR/$local_rel_path"

    echo "Checking remote path: $remote_path"
    # Check if the file exists on the remote device
    if adb shell "[ -f $remote_path ]" >/dev/null 2>&1; then
        echo "Pulling $remote_path -> $local_path"
        mkdir -p "$(dirname "$local_path")"
        adb pull "$remote_path" "$local_path"
    else
        echo "Remote file does not exist: $remote_path (skipping)"
    fi
}

# Helper to adb pull a directory if it exists
pull_dir() {
    local remote_path="$1"
    local local_rel_path="${remote_path#/}"
    local local_path="$DEST_DIR/$local_rel_path"

    echo "Checking remote directory: $remote_path"
    if adb shell "[ -d $remote_path ]" >/dev/null 2>&1; then
        echo "Pulling directory $remote_path -> $local_path"
        mkdir -p "$(dirname "$local_path")"
        adb pull "$remote_path" "$(dirname "$local_path")"
    else
        echo "Remote directory does not exist: $remote_path (skipping)"
    fi
}

# Pull required files
pull_file "/system/bin/app_process64"
pull_file "/system/framework/framework.jar"
pull_file "/system/lib64/libandroid_runtime.so"

# Some Android versions have core-oj and core-libart in /apex/com.android.art/javalib/
# and others in /system/framework/
pull_file "/apex/com.android.art/javalib/core-oj.jar"
pull_file "/apex/com.android.art/javalib/core-libart.jar"
pull_file "/apex/com.android.art/lib64/libart.so"

pull_file "/system/framework/core-oj.jar"
pull_file "/system/framework/core-libart.jar"
pull_file "/system/lib64/libart.so"

# Pull optional files/directories as specified in the import script
pull_dir "/system/framework"
pull_dir "/system/lib64"
pull_dir "/apex/com.android.art"
pull_dir "/apex/com.android.runtime"
pull_dir "/apex/com.android.conscrypt"
pull_dir "/apex/com.android.i18n"
pull_dir "/apex/com.android.tzdata"

echo "Successfully pulled files to $DEST_DIR"
