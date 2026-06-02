#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

NDK_PREBUILT=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64
CC=$NDK_PREBUILT/bin/aarch64-linux-android35-clang
CXX=$NDK_PREBUILT/bin/aarch64-linux-android35-clang++
NDK_SYSROOT=$NDK_PREBUILT/sysroot
SYSROOT_TMP="$ROOT_DIR/build/sysroot/data/local/tmp"
SYSROOT_LIB="$ROOT_DIR/build/sysroot/system/lib64"

mkdir -p "$ROOT_DIR/build/bin" "$SYSROOT_TMP" "$SYSROOT_LIB"

# --- libadd.so ---
echo "[compile] Building libadd.so ..."
$CC -shared -fPIC -Wl,-z,max-page-size=4096 \
    "$ROOT_DIR/tests/assets/elf/libadd.c" \
    -o "$SYSROOT_TMP/libadd.so"

# --- libjnitest.so ---


echo "[compile] Building libjnitest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libjnitest.c" \
    -llog \
    -o "$SYSROOT_TMP/libjnitest.so"

echo "[compile] Built: $SYSROOT_TMP/libjnitest.so"
file "$SYSROOT_TMP/libjnitest.so"

# --- JNI-only APK fixture ---
echo "[compile] Building libjnionlyapktest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libjnionlyapktest.c" \
    -llog \
    -o "$SYSROOT_TMP/libjnionlyapktest.so"

echo "[compile] Built: $SYSROOT_TMP/libjnionlyapktest.so"
file "$SYSROOT_TMP/libjnionlyapktest.so"

# --- libnativeactivitytest.so ---
echo "[compile] Building libnativeactivitytest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libnativeactivitytest.c" \
    -lEGL \
    -lGLESv2 \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libnativeactivitytest.so"

echo "[compile] Built: $SYSROOT_TMP/libnativeactivitytest.so"
file "$SYSROOT_TMP/libnativeactivitytest.so"

# --- libnativegluethreadtest.so ---
echo "[compile] Building libnativegluethreadtest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libnativegluethreadtest.c" \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libnativegluethreadtest.so"

echo "[compile] Built: $SYSROOT_TMP/libnativegluethreadtest.so"
file "$SYSROOT_TMP/libnativegluethreadtest.so"

# --- libnativeappgluecmdtest.so ---
echo "[compile] Building libnativeappgluecmdtest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libnativeappgluecmdtest.c" \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libnativeappgluecmdtest.so"

echo "[compile] Built: $SYSROOT_TMP/libnativeappgluecmdtest.so"
file "$SYSROOT_TMP/libnativeappgluecmdtest.so"

# --- APK-local dependency fixture ---
echo "[compile] Building libapkdepbase.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    "$ROOT_DIR/tests/assets/elf/libapkdepbase.c" \
    -o "$SYSROOT_TMP/libapkdepbase.so"

echo "[compile] Built: $SYSROOT_TMP/libapkdepbase.so"
file "$SYSROOT_TMP/libapkdepbase.so"

echo "[compile] Building libapkdephelper.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    "$ROOT_DIR/tests/assets/elf/libapkdephelper.c" \
    -L "$SYSROOT_TMP" \
    -lapkdepbase \
    -o "$SYSROOT_TMP/libapkdephelper.so"

echo "[compile] Built: $SYSROOT_TMP/libapkdephelper.so"
file "$SYSROOT_TMP/libapkdephelper.so"

echo "[compile] Building libapkdeptest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libapkdeptest.c" \
    -L "$SYSROOT_TMP" \
    -lapkdephelper \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libapkdeptest.so"

echo "[compile] Built: $SYSROOT_TMP/libapkdeptest.so"
file "$SYSROOT_TMP/libapkdeptest.so"

# --- APK unsupported import trap fixture ---
echo "[compile] Building libunsupportedimporttest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libunsupportedimporttest.c" \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libunsupportedimporttest.so"

echo "[compile] Built: $SYSROOT_TMP/libunsupportedimporttest.so"
file "$SYSROOT_TMP/libunsupportedimporttest.so"

# --- APK asset fixture ---
echo "[compile] Building libassettest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libassettest.c" \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libassettest.so"

echo "[compile] Built: $SYSROOT_TMP/libassettest.so"
file "$SYSROOT_TMP/libassettest.so"

# --- APK context fixture ---
echo "[compile] Building libcontexttest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libcontexttest.c" \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libcontexttest.so"

echo "[compile] Built: $SYSROOT_TMP/libcontexttest.so"
file "$SYSROOT_TMP/libcontexttest.so"

# --- APK binder/service-manager fixture ---
echo "[compile] Building libbindertest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libbindertest.c" \
    -lbinder_ndk \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libbindertest.so"

echo "[compile] Built: $SYSROOT_TMP/libbindertest.so"
file "$SYSROOT_TMP/libbindertest.so"

# --- APK binder transaction/parcel fixture ---
echo "[compile] Building libbindertranstest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libbindertranstest.c" \
    -lbinder_ndk \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libbindertranstest.so"

echo "[compile] Built: $SYSROOT_TMP/libbindertranstest.so"
file "$SYSROOT_TMP/libbindertranstest.so"

# --- APK local binder onTransact fixture ---
echo "[compile] Building libbinderlocaltest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libbinderlocaltest.c" \
    -lbinder_ndk \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libbinderlocaltest.so"

echo "[compile] Built: $SYSROOT_TMP/libbinderlocaltest.so"
file "$SYSROOT_TMP/libbinderlocaltest.so"

# --- APK binder string/allocator fixture ---
echo "[compile] Building libbinderstringtest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libbinderstringtest.c" \
    -lbinder_ndk \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libbinderstringtest.so"

echo "[compile] Built: $SYSROOT_TMP/libbinderstringtest.so"
file "$SYSROOT_TMP/libbinderstringtest.so"

# --- APK binder array/allocator fixture ---
echo "[compile] Building libbinderarraytest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libbinderarraytest.c" \
    -lbinder_ndk \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libbinderarraytest.so"

echo "[compile] Built: $SYSROOT_TMP/libbinderarraytest.so"
file "$SYSROOT_TMP/libbinderarraytest.so"

# --- APK binder parcelable/fd fixture ---
echo "[compile] Building libbinderparcelabletest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libbinderparcelabletest.c" \
    -lbinder_ndk \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libbinderparcelabletest.so"

echo "[compile] Built: $SYSROOT_TMP/libbinderparcelabletest.so"
file "$SYSROOT_TMP/libbinderparcelabletest.so"

# --- APK binder lifecycle fixture ---
echo "[compile] Building libbinderlifecycletest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libbinderlifecycletest.c" \
    -lbinder_ndk \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libbinderlifecycletest.so"

echo "[compile] Built: $SYSROOT_TMP/libbinderlifecycletest.so"
file "$SYSROOT_TMP/libbinderlifecycletest.so"

# --- APK binder driver realism fixture ---
echo "[compile] Building libbinderdrivertest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libbinderdrivertest.c" \
    -lbinder_ndk \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libbinderdrivertest.so"

echo "[compile] Built: $SYSROOT_TMP/libbinderdrivertest.so"
file "$SYSROOT_TMP/libbinderdrivertest.so"

# --- APK AIDL-style Binder smoke fixture ---
echo "[compile] Building libaidlsmoketest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libaidlsmoketest.c" \
    -lbinder_ndk \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libaidlsmoketest.so"

echo "[compile] Built: $SYSROOT_TMP/libaidlsmoketest.so"
file "$SYSROOT_TMP/libaidlsmoketest.so"

# --- APK generated-AIDL-style Binder fixture ---
echo "[compile] Building libaidlgeneratedtest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libaidlgeneratedtest.c" \
    -lbinder_ndk \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libaidlgeneratedtest.so"

echo "[compile] Built: $SYSROOT_TMP/libaidlgeneratedtest.so"
file "$SYSROOT_TMP/libaidlgeneratedtest.so"

# --- APK Binder weak/extension fixture ---
echo "[compile] Building libbinderweaktest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libbinderweaktest.c" \
    -lbinder_ndk \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libbinderweaktest.so"

echo "[compile] Built: $SYSROOT_TMP/libbinderweaktest.so"
file "$SYSROOT_TMP/libbinderweaktest.so"

# --- APK real generated-AIDL-source import fixture ---
echo "[compile] Building libaidlrealsourcetest.so ..."
$CXX -shared -fPIC \
    -std=c++17 \
    -fno-exceptions \
    -fno-rtti \
    -mgeneral-regs-only \
    -nostdlib++ \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    -I "$ROOT_DIR/tests/assets/aidl/generated" \
    "$ROOT_DIR/tests/assets/elf/libaidlrealsourcetest.cpp" \
    "$ROOT_DIR/tests/assets/aidl/generated/com/example/muplar/IRealAdder.cpp" \
    -lbinder_ndk \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libaidlrealsourcetest.so"

echo "[compile] Built: $SYSROOT_TMP/libaidlrealsourcetest.so"
file "$SYSROOT_TMP/libaidlrealsourcetest.so"

echo "[compile] Building libaidlndktest.so ..."

$CXX -shared -fPIC \
    -std=c++17 \
    -mbranch-protection=none \
    -Wl,-z,max-page-size=4096 \
    -I"$ROOT_DIR/platform/android-commons/libcpp-config" \
    -I"$ROOT_DIR/tests/assets/aidl/generated-ndk/include" \
    -I"$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/sysroot/usr/include" \
    -idirafter "$ROOT_DIR/platform/android-commons" \
    "$ROOT_DIR/tests/assets/elf/libaidlndktest.cpp" \
    "$ROOT_DIR/tests/assets/aidl/generated-ndk/cpp/com/example/muplar/IRealAdder.cpp" \
    -landroid \
    -llog \
    -lbinder_ndk \
    -lc++_shared \
    -o "$SYSROOT_TMP/libaidlndktest.so"

if [ $? -ne 0 ]; then
    echo "Building shared test binary error."
    exit 1
fi

echo "[compile] Built: $SYSROOT_TMP/libaidlndktest.so"
file "$SYSROOT_TMP/libaidlndktest.so"

# --- test_shared ---
# -nostartfiles: skip crtbegin_dynamic.o which calls __libc_init (needs real libc)
# We supply our own _start in test_shared_start.S that calls main() directly.
echo "[compile] Building test_shared ..."
$CC -fPIE -pie \
    -mgeneral-regs-only \
    -Wl,-z,max-page-size=4096 \
    -Wl,--dynamic-linker=/system/bin/linker64 \
    -nostartfiles \
    "$ROOT_DIR/tests/assets/elf/test_shared_start.S" \
    "$ROOT_DIR/tests/assets/elf/test_shared.c" \
    -L "$SYSROOT_TMP" \
    -ladd \
    -Wl,-rpath,/data/local/tmp \
    -o "$ROOT_DIR/build/bin/test_shared"

echo "[compile] file $ROOT_DIR/build/bin/test_shared:"
file "$ROOT_DIR/build/bin/test_shared"

# --- linker64 ---
echo "[compile] Building linker64 ..."
chmod +x "$ROOT_DIR/linker64/build-linker64.sh"
"$ROOT_DIR/linker64/build-linker64.sh"

echo "Building native aidl..."
if [ ! -f "$SYSROOT_TMP/libc++_shared.so" ]; then
    "$ROOT_DIR/tools/prepare-android-sysroot.sh" \
        --sysroot "$ROOT_DIR/build/sysroot" \
        --no-android-root \
        --no-art-bootstrap
fi
"$ROOT_DIR/tests/assets/apk/create-native-aidl-ndk-apk.sh"
