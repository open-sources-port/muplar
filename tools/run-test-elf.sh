#!/bin/zsh

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ANGLE_DYLIB_DIR="$ROOT_DIR/third_party/angle-bin"
if [ -d "$ANGLE_DYLIB_DIR" ]; then
    export DYLD_LIBRARY_PATH="$ANGLE_DYLIB_DIR${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
fi
MUP="$ROOT_DIR/build/bin/mup"

NDK_SYSROOT=$NDK_PREBUILT/sysroot
SYSROOT_TMP="$ROOT_DIR/build/sysroot/data/local/tmp"
SYSROOT_LIB="$ROOT_DIR/build/sysroot/system/lib64"

export scriptToRun=$ROOT_DIR/tools/build-all-local-only.sh
chmod +x ${scriptToRun}
zsh ${scriptToRun}

ELF="$ROOT_DIR/build/bin/test_return_42"
echo "========================\nCalling $ELF..."
"$MUP" "$ELF"
echo "Exit code: $?"

ELF="$ROOT_DIR/build/bin/simple_app_with_print"
echo "========================\nCalling $ELF..."
"$MUP" "$ELF"
echo "Exit code: $?"

ELF="$ROOT_DIR/build/bin/test_shared"
echo "========================\nCalling $ELF..."
"$MUP" --sysroot "$ROOT_DIR/build/sysroot" "$ELF"
echo "Exit code: $?"

ELF="$SYSROOT_TMP/libjnitest.so"
echo "========================\nCalling $ELF..."
"$MUP" --sysroot "$ROOT_DIR/build/sysroot" "$ELF"
echo "Exit code: $?"

ELF="$SYSROOT_TMP/libnativeactivitytest.so"
echo "========================\nCalling $ELF..."
"$MUP" --native-activity --sysroot "$ROOT_DIR/build/sysroot" "$ELF"
echo "Exit code: $?"

ELF="$SYSROOT_TMP/libnativegluethreadtest.so"
echo "========================\nCalling $ELF..."
"$MUP" --native-activity --sysroot "$ROOT_DIR/build/sysroot" "$ELF"
echo "Exit code: $?"

ELF="$SYSROOT_TMP/libnativeappgluecmdtest.so"
echo "========================\nCalling $ELF..."
"$MUP" --native-activity --sysroot "$ROOT_DIR/build/sysroot" "$ELF"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativeappgluecmdtest.apk"
echo "========================\nCalling $APK..."
"$MUP" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativeapkdeptest.apk"
echo "========================\nCalling $APK..."
"$MUP" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativejnionlytest.apk"
JNI_ONLY_LOG="$ROOT_DIR/build/nativejnionlytest.log"
echo "========================\nCalling $APK..."
"$MUP" --sysroot "$ROOT_DIR/build/sysroot" "$APK" > "$JNI_ONLY_LOG" 2>&1
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
"$MUP" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

STRICT_LOG="$ROOT_DIR/build/nativeunsupportedimporttest-strict.log"
echo "========================\nCalling $APK with --strict-direct-imports (expected failure)..."
"$MUP" --strict-direct-imports --sysroot "$ROOT_DIR/build/sysroot" "$APK" > "$STRICT_LOG" 2>&1
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
javaOnlyLog="$(sed -n 's/^log: //p' "$SCAN_LOG" | head -1)"
if [ "$scanCode" -eq 0 ]; then
    if ! grep -q "status: launch-ok" "$SCAN_LOG"; then
        echo "Compatibility scan passed Java-only APK without launch-ok status."
        exit 1
    fi
    if [ -z "$javaOnlyLog" ] ||
       ! grep -q "ArtApkMain.main returned successfully" "$javaOnlyLog"; then
        echo "Java-only APK did not complete through the host JVM bootstrap path."
        exit 1
    fi
else
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
    if [ -z "$javaOnlyLog" ] ||
       ! grep -q "muplar-art-bootstrap.jar" "$javaOnlyLog"; then
        echo "Compatibility scan did not include Muplar ART bootstrap jar in the plan."
        exit 1
    fi
fi

"$ROOT_DIR/tools/run-apk-compat-scan.sh" --report "$SCAN_REPORT" "$SYSROOT_TMP/tinyjavaactivity.apk" > "$SCAN_LOG" 2>&1
scanCode=$?
cat "$SCAN_LOG"
tinyJavaLog="$(sed -n 's/^log: //p' "$SCAN_LOG" | head -1)"
if [ "$scanCode" -eq 0 ]; then
    if ! grep -q "status: launch-ok" "$SCAN_LOG"; then
        echo "Compatibility scan passed tiny Java Activity APK without launch-ok status."
        exit 1
    fi
    if [ -z "$tinyJavaLog" ] ||
       ! grep -q "ArtApkMain.main returned successfully" "$tinyJavaLog"; then
        echo "Tiny Java Activity did not complete through the host JVM bootstrap path."
        exit 1
    fi
else
    if ! grep -q "status: java-runtime-required" "$SCAN_LOG"; then
        echo "Compatibility scan did not classify tiny Java Activity APK correctly."
        exit 1
    fi
    if [ -z "$tinyJavaLog" ] ||
       ! grep -q "launch activity=com.example.muplar.tiny.TinyActivity" "$tinyJavaLog"; then
        echo "Tiny Java Activity launch target was not recorded in the ART plan."
        exit 1
    fi
    if ! grep -q "muplar-art-bootstrap.jar" "$tinyJavaLog"; then
        echo "Tiny Java Activity ART plan did not include the Muplar bootstrap jar."
        exit 1
    fi
fi

"$ROOT_DIR/tools/run-apk-compat-scan.sh" --report "$SCAN_REPORT" "$SYSROOT_TMP/javalauncher.apk" > "$SCAN_LOG" 2>&1
scanCode=$?
cat "$SCAN_LOG"
launcherJavaLog="$(sed -n 's/^log: //p' "$SCAN_LOG" | head -1)"
if [ "$scanCode" -eq 0 ]; then
    if ! grep -q "status: launch-ok" "$SCAN_LOG"; then
        echo "Compatibility scan passed Java launcher APK without launch-ok status."
        exit 1
    fi
    if [ -z "$launcherJavaLog" ] ||
       ! grep -q "ArtApkMain.main returned successfully" "$launcherJavaLog"; then
        echo "Java launcher APK did not complete through the host JVM bootstrap path."
        exit 1
    fi
    if ! grep -q "\[LauncherActivity\] onCreate called" "$launcherJavaLog"; then
        echo "Java launcher APK did not invoke onCreate."
        exit 1
    fi
    if ! grep -q "\[LauncherActivity\] Launching app: com.example.muplar.tiny" "$launcherJavaLog"; then
        echo "Java launcher APK did not print launch message."
        exit 1
    fi
else
    echo "Compatibility scan failed for Java launcher APK."
    exit 1
fi

EXTERNAL_JAVA_APK="$ROOT_DIR/build/javaonlytest-external.apk"
cp "$SYSROOT_TMP/javaonlytest.apk" "$EXTERNAL_JAVA_APK"
"$ROOT_DIR/tools/run-apk-compat-scan.sh" --report "$SCAN_REPORT" "$EXTERNAL_JAVA_APK" > "$SCAN_LOG" 2>&1
scanCode=$?
cat "$SCAN_LOG"
externalJavaLog="$(sed -n 's/^log: //p' "$SCAN_LOG" | head -1)"
if [ "$scanCode" -eq 0 ]; then
    if ! grep -q "status: launch-ok" "$SCAN_LOG"; then
        echo "Compatibility scan passed external Java-only APK without launch-ok status."
        exit 1
    fi
    if [ -z "$externalJavaLog" ] ||
       ! grep -q "ArtApkMain.main returned successfully" "$externalJavaLog"; then
        echo "External Java-only APK did not complete through the host JVM bootstrap path."
        exit 1
    fi
else
    if ! grep -q "status: java-runtime-required" "$SCAN_LOG"; then
        echo "Compatibility scan did not classify external Java-only APK correctly."
        exit 1
    fi
    if [ -z "$externalJavaLog" ] || ! grep -q "staged APK for guest path" "$externalJavaLog"; then
        echo "External Java-only APK was not staged into the guest sysroot."
        exit 1
    fi
    if ! grep -q "guest apk=/data/local/tmp/muplar/apks/" "$externalJavaLog"; then
        echo "External Java-only APK did not get a guest-visible APK path."
        exit 1
    fi
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
"$MUP" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativecontexttest.apk"
echo "========================\nCalling $APK..."
"$MUP" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativebindertest.apk"
echo "========================\nCalling $APK..."
"$MUP" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativebindertranstest.apk"
echo "========================\nCalling $APK..."
"$MUP" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativebinderlocaltest.apk"
echo "========================\nCalling $APK..."
"$MUP" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativebinderstringtest.apk"
echo "========================\nCalling $APK..."
"$MUP" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativebinderarraytest.apk"
echo "========================\nCalling $APK..."
"$MUP" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativebinderparcelabletest.apk"
echo "========================\nCalling $APK..."
"$MUP" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativebinderlifecycletest.apk"
echo "========================\nCalling $APK..."
"$MUP" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativebinderdrivertest.apk"
echo "========================\nCalling $APK..."
"$MUP" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativeaidlsmoketest.apk"
echo "========================\nCalling $APK..."
"$MUP" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativeaidlgeneratedtest.apk"
echo "========================\nCalling $APK..."
"$MUP" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativebinderweaktest.apk"
echo "========================\nCalling $APK..."
"$MUP" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativeaidlrealsourcetest.apk"
echo "========================\nCalling $APK..."
"$MUP" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

APK="$SYSROOT_TMP/nativeaidlndktest.apk"
echo "========================\nCalling $APK..."
"$MUP" --sysroot "$ROOT_DIR/build/sysroot" "$APK"
echo "Exit code: $?"

echo "Script run finished!"
