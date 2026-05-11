#!/bin/zsh

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ELF="$ROOT_DIR/build/bin/test_return_42"

echo "ROOT [${ROOT_DIR}]"
echo "ANDROID SDK [${ANDROID_NDK_HOME}]"
echo "Cleaning up build folder..."
rm -rfv build/*

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

echo "Building test binary..."
sh $ROOT_DIR/tests/assets/elf/compile.sh
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building test binary error."
  exit 1
fi
echo "Building test binary done."

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
"$ROOT_DIR/build/bin/mup" "$ELF"
echo "Script run finished!"
