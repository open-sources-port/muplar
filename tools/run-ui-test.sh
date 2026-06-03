#!/bin/zsh

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

NDK_SYSROOT=$NDK_PREBUILT/sysroot
SYSROOT_TMP="$ROOT_DIR/build/sysroot/data/local/tmp"
SYSROOT_LIB="$ROOT_DIR/build/sysroot/system/lib64"

echo "ROOT [${ROOT_DIR}]"
echo "ANDROID SDK [${ANDROID_NDK_HOME}]"
sh $ROOT_DIR/tools/clean-build.sh

echo "Configuring source code..."
cmake -B build -G Ninja
echo "Configuring source code done."

echo "Building the source code..."
cmake --build build
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building the source code error."
  exit 1
fi
echo "Building the source code done."

echo "========================================="
echo "Preparing Android ARM64 sysroot inputs..."
"$ROOT_DIR/tools/prepare-android-sysroot.sh" \
    --sysroot "$ROOT_DIR/build/sysroot" \
    --no-android-root
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Preparing Android ARM64 sysroot inputs error."
  exit 1
fi
echo "Preparing Android ARM64 sysroot inputs done."

echo "========================================="
echo "Building basic test binary..."
export scriptToRun=$ROOT_DIR/tests/assets/elf/compile-basic.sh
chmod +x ${scriptToRun}
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building basic test binary error."
  exit 1
fi
echo "Building basic test binary done."

echo "========================================="
echo "Building shared test binary..."
export scriptToRun=$ROOT_DIR/tests/assets/elf/compile-shared-lib.sh
chmod +x ${scriptToRun}
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building shared test binary error."
  exit 1
fi
echo "Building shared test binary done."

echo "========================================="
echo "Running Muplar ELF loader..."
echo "codesign -d --entitlements - $ROOT_DIR/build/bin/mup"
cat > $ROOT_DIR/mup.entitlements << 'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.security.hypervisor</key>
    <true/>
</dict>
</plist>
EOF
codesign --entitlements mup.entitlements --force -s - $ROOT_DIR/build/bin/mup
codesign -d --entitlements - $ROOT_DIR/build/bin/mup 2>&1  # verify it took

ELF="$SYSROOT_TMP/libnativeactivitytest.so"
echo "========================\nCalling $ELF..."
"$ROOT_DIR/build/bin/mup" --native-activity --sysroot "$ROOT_DIR/build/sysroot" --host-window "$ELF"
echo "Exit code: $?"
