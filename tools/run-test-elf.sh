#!/bin/zsh

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

NDK_SYSROOT=$NDK_PREBUILT/sysroot
SYSROOT_TMP="$ROOT_DIR/build/sysroot/data/local/tmp"
SYSROOT_LIB="$ROOT_DIR/build/sysroot/system/lib64"

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

ELF="$ROOT_DIR/build/bin/test_return_42"
echo "========================\nCalling $ELF..."
"$ROOT_DIR/build/bin/mup" "$ELF"
echo "Exit code: $?"

ELF="$ROOT_DIR/build/bin/simple_app_with_print"
echo "========================\nCalling $ELF..."
"$ROOT_DIR/build/bin/mup" "$ELF"
echo "Exit code: $?"

ELF="$ROOT_DIR/build/bin/test_shared"
echo "========================\nCalling $ELF..."
"$ROOT_DIR/build/bin/mup" --sysroot "$ROOT_DIR/build/sysroot" "$ELF"
echo "Exit code: $?"

ELF="$SYSROOT_TMP/libjnitest.so"
echo "========================\nCalling $ELF..."
"$ROOT_DIR/build/bin/mup" --sysroot "$ROOT_DIR/build/sysroot" "$ELF"
echo "Exit code: $?"

ELF="$SYSROOT_TMP/libnativeactivitytest.so"
echo "========================\nCalling $ELF..."
"$ROOT_DIR/build/bin/mup" --native-activity --sysroot "$ROOT_DIR/build/sysroot" "$ELF"
echo "Exit code: $?"

ELF="$SYSROOT_TMP/libnativegluethreadtest.so"
echo "========================\nCalling $ELF..."
"$ROOT_DIR/build/bin/mup" --native-activity --sysroot "$ROOT_DIR/build/sysroot" "$ELF"
echo "Exit code: $?"

ELF="$SYSROOT_TMP/libnativeappgluecmdtest.so"
echo "========================\nCalling $ELF..."
"$ROOT_DIR/build/bin/mup" --native-activity --sysroot "$ROOT_DIR/build/sysroot" "$ELF"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativeappgluecmdtest.apk"
echo "========================\nCalling $APK..."
"$ROOT_DIR/build/bin/mup" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativeapkdeptest.apk"
echo "========================\nCalling $APK..."
"$ROOT_DIR/build/bin/mup" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativejnionlytest.apk"
JNI_ONLY_LOG="$ROOT_DIR/build/nativejnionlytest.log"
echo "========================\nCalling $APK..."
"$ROOT_DIR/build/bin/mup" --sysroot "$ROOT_DIR/build/sysroot" "$APK" > "$JNI_ONLY_LOG" 2>&1
jniOnlyCode=$?
cat "$JNI_ONLY_LOG"
echo "Exit code: $jniOnlyCode"
if [ "$jniOnlyCode" -ne 0 ]; then
    echo "JNI-only APK test failed."
    exit 1
fi
if ! grep -q "JNI_OnLoad-only APK path complete" "$JNI_ONLY_LOG"; then
    echo "JNI-only APK did not complete through the JNI_OnLoad-only path."
    exit 1
fi
if ! grep -q "patched .* unaligned zero-vector stack store" "$JNI_ONLY_LOG"; then
    echo "JNI-only APK did not exercise the unaligned vector stack patch."
    exit 1
fi

APK="$SYSROOT_TMP/nativeunsupportedimporttest.apk"
echo "========================\nCalling $APK..."
"$ROOT_DIR/build/bin/mup" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

STRICT_LOG="$ROOT_DIR/build/nativeunsupportedimporttest-strict.log"
echo "========================\nCalling $APK with --strict-direct-imports (expected failure)..."
"$ROOT_DIR/build/bin/mup" --strict-direct-imports --sysroot "$ROOT_DIR/build/sysroot" "$APK" > "$STRICT_LOG" 2>&1
strictCode=$?
cat "$STRICT_LOG"
echo "Exit code: $strictCode"
if [ "$strictCode" -eq 0 ]; then
    echo "Strict direct import test unexpectedly succeeded."
    exit 1
fi
if ! grep -q "strict direct import mode" "$STRICT_LOG"; then
    echo "Strict direct import test did not report strict-mode failure."
    exit 1
fi

SCAN_LOG="$ROOT_DIR/build/apk-compat-scan-test.log"
SCAN_REPORT="$ROOT_DIR/build/apk-compat-scan-test.md"
echo "========================\nRunning APK compatibility scan smoke tests..."
"$ROOT_DIR/tools/run-apk-compat-scan.sh" --report "$SCAN_REPORT" "$SYSROOT_TMP/nativeapkdeptest.apk" > "$SCAN_LOG" 2>&1
scanCode=$?
cat "$SCAN_LOG"
if [ "$scanCode" -ne 0 ]; then
    echo "Compatibility scan unexpectedly failed for dependency APK."
    exit 1
fi
if ! grep -q "status: launch-ok" "$SCAN_LOG"; then
    echo "Compatibility scan did not mark dependency APK as launch-ok."
    exit 1
fi
if ! grep -q "No missing APK-local libraries or unresolved direct imports found." "$SCAN_REPORT"; then
    echo "Compatibility scan report did not show a clean dependency APK backlog."
    exit 1
fi

"$ROOT_DIR/tools/run-apk-compat-scan.sh" --report "$SCAN_REPORT" "$SYSROOT_TMP/nativejnionlytest.apk" > "$SCAN_LOG" 2>&1
scanCode=$?
cat "$SCAN_LOG"
if [ "$scanCode" -ne 0 ]; then
    echo "Compatibility scan unexpectedly failed for JNI-only APK."
    exit 1
fi
if ! grep -q "status: launch-ok" "$SCAN_LOG"; then
    echo "Compatibility scan did not mark JNI-only APK as launch-ok."
    exit 1
fi
if ! grep -q "No missing APK-local libraries or unresolved direct imports found." "$SCAN_REPORT"; then
    echo "Compatibility scan report did not show a clean JNI-only APK backlog."
    exit 1
fi

ART_SYSROOT_LOG="$ROOT_DIR/build/android-art-sysroot-check.log"
echo "========================\nRunning Android ART sysroot inventory check (expected incomplete)..."
"$ROOT_DIR/tools/check-android-art-sysroot.sh" --sysroot "$ROOT_DIR/build/sysroot" > "$ART_SYSROOT_LOG" 2>&1
artCheckCode=$?
cat "$ART_SYSROOT_LOG"
if [ "$artCheckCode" -eq 0 ]; then
    echo "Generated test sysroot unexpectedly has a complete ART runtime."
    exit 1
fi
if ! grep -q "status: art-sysroot-incomplete" "$ART_SYSROOT_LOG"; then
    echo "ART sysroot check did not report the expected incomplete status."
    exit 1
fi
if ! grep -q "app_process64" "$ART_SYSROOT_LOG"; then
    echo "ART sysroot check did not report the missing ART executable."
    exit 1
fi

"$ROOT_DIR/tools/run-apk-compat-scan.sh" --report "$SCAN_REPORT" "$SYSROOT_TMP/javaonlytest.apk" > "$SCAN_LOG" 2>&1
scanCode=$?
cat "$SCAN_LOG"
if [ "$scanCode" -eq 0 ]; then
    echo "Compatibility scan unexpectedly passed Java-only APK."
    exit 1
fi
if ! grep -q "status: java-runtime-required" "$SCAN_LOG"; then
    echo "Compatibility scan did not classify Java-only APK correctly."
    exit 1
fi
if ! grep -q "Java/ART Runtime Required" "$SCAN_REPORT"; then
    echo "Compatibility scan report did not include the Java/ART backlog."
    exit 1
fi
if ! grep -q "app_process64" "$SCAN_REPORT"; then
    echo "Compatibility scan report did not include missing ART executable."
    exit 1
fi

EXTERNAL_JAVA_APK="$ROOT_DIR/build/javaonlytest-external.apk"
cp "$SYSROOT_TMP/javaonlytest.apk" "$EXTERNAL_JAVA_APK"
"$ROOT_DIR/tools/run-apk-compat-scan.sh" --report "$SCAN_REPORT" "$EXTERNAL_JAVA_APK" > "$SCAN_LOG" 2>&1
scanCode=$?
cat "$SCAN_LOG"
if [ "$scanCode" -eq 0 ]; then
    echo "Compatibility scan unexpectedly passed external Java-only APK."
    exit 1
fi
if ! grep -q "status: java-runtime-required" "$SCAN_LOG"; then
    echo "Compatibility scan did not classify external Java-only APK correctly."
    exit 1
fi
externalJavaLog="$(sed -n 's/^log: //p' "$SCAN_LOG" | head -1)"
if [ -z "$externalJavaLog" ] || ! grep -q "staged APK for guest path" "$externalJavaLog"; then
    echo "External Java-only APK was not staged into the guest sysroot."
    exit 1
fi
if ! grep -q "guest apk=/data/local/tmp/muplar/apks/" "$externalJavaLog"; then
    echo "External Java-only APK did not get a guest-visible APK path."
    exit 1
fi

"$ROOT_DIR/tools/run-apk-compat-scan.sh" --report "$SCAN_REPORT" "$SYSROOT_TMP/nativeunsupportedimporttest.apk" > "$SCAN_LOG" 2>&1
scanCode=$?
cat "$SCAN_LOG"
if [ "$scanCode" -eq 0 ]; then
    echo "Compatibility scan unexpectedly passed unsupported-import APK."
    exit 1
fi
if ! grep -q "status: native-deps-incomplete" "$SCAN_LOG"; then
    echo "Compatibility scan did not classify unsupported-import APK correctly."
    exit 1
fi
if ! grep -q 'libunsupportedimporttest.so needs muplar_missing_native_import' "$SCAN_REPORT"; then
    echo "Compatibility scan report did not include the unsupported import backlog."
    exit 1
fi

"$ROOT_DIR/tools/run-apk-compat-scan.sh" --report "$SCAN_REPORT" "$SYSROOT_TMP/nativeaidlndktest.apk" > "$SCAN_LOG" 2>&1
scanCode=$?
cat "$SCAN_LOG"
if [ "$scanCode" -ne 0 ]; then
    echo "Compatibility scan unexpectedly failed for NDK AIDL APK."
    exit 1
fi
if ! grep -q "status: launch-ok" "$SCAN_LOG"; then
    echo "Compatibility scan did not mark NDK AIDL APK as launch-ok."
    exit 1
fi
if ! grep -q "No missing APK-local libraries or unresolved direct imports found." "$SCAN_REPORT"; then
    echo "Compatibility scan report did not show a clean NDK AIDL backlog."
    exit 1
fi

APK="$SYSROOT_TMP/nativeassettest.apk"
echo "========================\nCalling $APK..."
"$ROOT_DIR/build/bin/mup" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativecontexttest.apk"
echo "========================\nCalling $APK..."
"$ROOT_DIR/build/bin/mup" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativebindertest.apk"
echo "========================\nCalling $APK..."
"$ROOT_DIR/build/bin/mup" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativebindertranstest.apk"
echo "========================\nCalling $APK..."
"$ROOT_DIR/build/bin/mup" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativebinderlocaltest.apk"
echo "========================\nCalling $APK..."
"$ROOT_DIR/build/bin/mup" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativebinderstringtest.apk"
echo "========================\nCalling $APK..."
"$ROOT_DIR/build/bin/mup" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativebinderarraytest.apk"
echo "========================\nCalling $APK..."
"$ROOT_DIR/build/bin/mup" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativebinderparcelabletest.apk"
echo "========================\nCalling $APK..."
"$ROOT_DIR/build/bin/mup" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativebinderlifecycletest.apk"
echo "========================\nCalling $APK..."
"$ROOT_DIR/build/bin/mup" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativebinderdrivertest.apk"
echo "========================\nCalling $APK..."
"$ROOT_DIR/build/bin/mup" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativeaidlsmoketest.apk"
echo "========================\nCalling $APK..."
"$ROOT_DIR/build/bin/mup" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativeaidlgeneratedtest.apk"
echo "========================\nCalling $APK..."
"$ROOT_DIR/build/bin/mup" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativebinderweaktest.apk"
echo "========================\nCalling $APK..."
"$ROOT_DIR/build/bin/mup" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativeaidlrealsourcetest.apk"
echo "========================\nCalling $APK..."
"$ROOT_DIR/build/bin/mup" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativeaidlndktest.apk"
echo "========================\nCalling $APK..."
"$ROOT_DIR/build/bin/mup" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

echo "Script run finished!"
