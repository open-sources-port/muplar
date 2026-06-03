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
echo "Building APK launch-envelope test..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-activity-apk.sh
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building APK launch-envelope test error."
  exit 1
fi
echo "Building APK launch-envelope test done."

echo "========================================="
echo "Building APK dependency test..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-dependency-apk.sh
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building APK dependency test error."
  exit 1
fi
echo "Building APK dependency test done."

echo "========================================="
echo "Building APK JNI-only test..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-jni-only-apk.sh
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building APK JNI-only test error."
  exit 1
fi
echo "Building APK JNI-only test done."

echo "========================================="
echo "Building Java-only APK classification test..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-java-only-apk.sh
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building Java-only APK classification test error."
  exit 1
fi
echo "Building Java-only APK classification test done."

echo "========================================="
echo "Building tiny Java Activity APK test..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-tiny-java-activity-apk.sh
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building tiny Java Activity APK test error."
  exit 1
fi
echo "Building tiny Java Activity APK test done."

echo "========================================="
echo "Building Java ART bootstrap jar..."
export scriptToRun=$ROOT_DIR/tools/build-art-bootstrap-jar.sh
sh ${scriptToRun} --sysroot "$ROOT_DIR/build/sysroot"
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building Java ART bootstrap jar error."
  exit 1
fi
echo "Building Java ART bootstrap jar done."

echo "========================================="
echo "Building APK unsupported import trap test..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-unsupported-import-apk.sh
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building APK unsupported import trap test error."
  exit 1
fi
echo "Building APK unsupported import trap test done."

echo "========================================="
echo "Building APK asset test..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-assets-apk.sh
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building APK asset test error."
  exit 1
fi
echo "Building APK asset test done."

echo "========================================="
echo "Building APK context test..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-context-apk.sh
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building APK context test error."
  exit 1
fi
echo "Building APK context test done."

echo "========================================="
echo "Building APK binder test..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-binder-apk.sh
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building APK binder test error."
  exit 1
fi
echo "Building APK binder test done."

echo "========================================="
echo "Building APK binder transaction test..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-binder-trans-apk.sh
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building APK binder transaction test error."
  exit 1
fi
echo "Building APK binder transaction test done."

echo "========================================="
echo "Building APK local binder test..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-binder-local-apk.sh
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building APK local binder test error."
  exit 1
fi
echo "Building APK local binder test done."

echo "========================================="
echo "Building APK binder string test..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-binder-string-apk.sh
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building APK binder string test error."
  exit 1
fi
echo "Building APK binder string test done."

echo "========================================="
echo "Building APK binder array test..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-binder-array-apk.sh
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building APK binder array test error."
  exit 1
fi
echo "Building APK binder array test done."

echo "========================================="
echo "Building APK binder parcelable test..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-binder-parcelable-apk.sh
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building APK binder parcelable test error."
  exit 1
fi
echo "Building APK binder parcelable test done."

echo "========================================="
echo "Building APK binder lifecycle test..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-binder-lifecycle-apk.sh
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building APK binder lifecycle test error."
  exit 1
fi
echo "Building APK binder lifecycle test done."

echo "========================================="
echo "Building APK binder driver test..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-binder-driver-apk.sh
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building APK binder driver test error."
  exit 1
fi
echo "Building APK binder driver test done."

echo "========================================="
echo "Building APK AIDL smoke test..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-aidl-smoke-apk.sh
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building APK AIDL smoke test error."
  exit 1
fi
echo "Building APK AIDL smoke test done."

echo "========================================="
echo "Building APK generated AIDL coverage test..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-aidl-generated-apk.sh
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building APK generated AIDL coverage test error."
  exit 1
fi
echo "Building APK generated AIDL coverage test done."

echo "========================================="
echo "Building APK binder weak/extension test..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-binder-weak-apk.sh
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building APK binder weak/extension test error."
  exit 1
fi
echo "Building APK binder weak/extension test done."

echo "========================================="
echo "Building APK real generated AIDL source test..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-aidl-real-source-apk.sh
sh ${scriptToRun}
returnCode=$?
if [ "$returnCode" -ne 0 ]; then
    echo "Building APK real generated AIDL source test error."
  exit 1
fi
echo "Building APK real generated AIDL source test done."

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
