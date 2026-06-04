#!/bin/zsh
set -e

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
BUSYBOX_SRC="${ROOT_DIR}/third_party/busybox"

if [ -z "$ANDROID_NDK_HOME" ]; then
    ANDROID_NDK_HOME="/opt/homebrew/share/android-ndk"
fi

if [ ! -d "$ANDROID_NDK_HOME" ]; then
    echo "ERROR: ANDROID_NDK_HOME is not set or does not exist at $ANDROID_NDK_HOME"
    exit 1
fi

NDK_PREBUILT="${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/darwin-x86_64"
if [ ! -d "$NDK_PREBUILT" ]; then
    echo "ERROR: NDK prebuilt directory not found at $NDK_PREBUILT"
    exit 1
fi

# Find the target-specific Clang compilers
CC_ARM64=$(find "$NDK_PREBUILT/bin" -name "aarch64-linux-android*-clang" | head -n 1)
CC_X86_64=$(find "$NDK_PREBUILT/bin" -name "x86_64-linux-android*-clang" | head -n 1)

AR="${NDK_PREBUILT}/bin/llvm-ar"
RANLIB="${NDK_PREBUILT}/bin/llvm-ranlib"
NM="${NDK_PREBUILT}/bin/llvm-nm"
STRIP="${NDK_PREBUILT}/bin/llvm-strip"

if [ -z "$CC_ARM64" ] || [ ! -x "$CC_ARM64" ]; then
    echo "ERROR: AArch64 NDK compiler not found under $NDK_PREBUILT/bin"
    exit 1
fi

if [ -z "$CC_X86_64" ] || [ ! -x "$CC_X86_64" ]; then
    echo "ERROR: x86_64 NDK compiler not found under $NDK_PREBUILT/bin"
    exit 1
fi

echo "Using ARM64 compiler: $CC_ARM64"
echo "Using x86_64 compiler: $CC_X86_64"

# Set up build directories
BUILD_DIR_ARM64="${BUILD_DIR}/busybox-aarch64"
BUILD_DIR_X86_64="${BUILD_DIR}/busybox-x86_64"

mkdir -p "$BUILD_DIR_ARM64"
mkdir -p "$BUILD_DIR_X86_64"
mkdir -p "${BUILD_DIR}/bin"

# -----------------------------------------------------------------------------
# Function to build BusyBox
# -----------------------------------------------------------------------------
build_busybox() {
    local arch="$1"
    local cc="$2"
    local build_dir="$3"
    local output_bin="${BUILD_DIR}/bin/busybox_${arch}"

    echo "=== Building BusyBox for ${arch} ==="

    # 1. Generate default Android configuration
    cd "$BUSYBOX_SRC"
    make O="$build_dir" android_ndk_defconfig

    # 2. Modify config
    cd "$build_dir"
    
    # Disable features not supported by Android Bionic
    sed -i.bak 's/CONFIG_SWAPON=y/# CONFIG_SWAPON is not set/' .config
    sed -i.bak 's/CONFIG_SWAPOFF=y/# CONFIG_SWAPOFF is not set/' .config
    sed -i.bak 's/CONFIG_TC=y/# CONFIG_TC is not set/' .config
    
    # Disable x86-only HW acceleration on non-x86 architectures
    sed -i.bak 's/CONFIG_SHA1_HWACCEL=y/# CONFIG_SHA1_HWACCEL is not set/' .config
    sed -i.bak 's/CONFIG_SHA256_HWACCEL=y/# CONFIG_SHA256_HWACCEL is not set/' .config
    
    # Resolve all other new configurations non-interactively
    yes "" | make -C "$BUSYBOX_SRC" O="$build_dir" oldconfig

    # Enable static compilation
    sed -i.bak 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
    # Enable ash shell, its aliases, and options
    sed -i.bak 's/# CONFIG_ASH is not set/CONFIG_ASH=y/' .config
    sed -i.bak 's/# CONFIG_ASH_BASH_COMPAT is not set/CONFIG_ASH_BASH_COMPAT=y/' .config
    sed -i.bak 's/# CONFIG_ASH_JOB_CONTROL is not set/CONFIG_ASH_JOB_CONTROL=y/' .config
    sed -i.bak 's/# CONFIG_ASH_ALIAS is not set/CONFIG_ASH_ALIAS=y/' .config
    sed -i.bak 's/# CONFIG_ASH_GETOPTS is not set/CONFIG_ASH_GETOPTS=y/' .config
    sed -i.bak 's/# CONFIG_ASH_ECHO is not set/CONFIG_ASH_ECHO=y/' .config
    sed -i.bak 's/# CONFIG_ASH_PRINTF is not set/CONFIG_ASH_PRINTF=y/' .config
    sed -i.bak 's/# CONFIG_ASH_TEST is not set/CONFIG_ASH_TEST=y/' .config
    sed -i.bak 's/# CONFIG_ASH_CMDCMD is not set/CONFIG_ASH_CMDCMD=y/' .config
    sed -i.bak 's/# CONFIG_ASH_RANDOM_SUPPORT is not set/CONFIG_ASH_RANDOM_SUPPORT=y/' .config
    sed -i.bak 's/# CONFIG_ASH_EXPAND_PRMT is not set/CONFIG_ASH_EXPAND_PRMT=y/' .config
    sed -i.bak 's/# CONFIG_FEATURE_PREFER_APPLETS is not set/CONFIG_FEATURE_PREFER_APPLETS=y/' .config
    sed -i.bak 's/# CONFIG_FEATURE_SH_STANDALONE is not set/CONFIG_FEATURE_SH_STANDALONE=y/' .config
    sed -i.bak 's/# CONFIG_FEATURE_SH_NOFORK is not set/CONFIG_FEATURE_SH_NOFORK=y/' .config

    # Alias sh and bash to ash
    sed -i.bak 's/CONFIG_SH_IS_NONE=y/# CONFIG_SH_IS_NONE is not set/' .config
    sed -i.bak 's/# CONFIG_SH_IS_ASH is not set/CONFIG_SH_IS_ASH=y/' .config
    sed -i.bak 's/CONFIG_BASH_IS_NONE=y/# CONFIG_BASH_IS_NONE is not set/' .config
    sed -i.bak 's/# CONFIG_BASH_IS_ASH is not set/CONFIG_BASH_IS_ASH=y/' .config
    # Clear old GCC-specific or 32-bit ARM-specific flags to let modern NDK Clang use its defaults.
    # BusyBox intentionally undefines HAVE_STRCHRNUL on Android for older bionic,
    # but modern NDK libc.a provides strchrnul. Rename BusyBox's compatibility
    # implementation so clean static links do not collide with bionic.
    sed -i.bak 's/CONFIG_EXTRA_CFLAGS=.*/CONFIG_EXTRA_CFLAGS="-Dstrchrnul=bb_strchrnul"/' .config
    sed -i.bak 's/CONFIG_EXTRA_LDFLAGS=.*/CONFIG_EXTRA_LDFLAGS=""/' .config
    sed -i.bak 's/CONFIG_EXTRA_LDLIBS=.*/CONFIG_EXTRA_LDLIBS=""/' .config
    # Clear old compiler prefix and sysroot to let NDK Clang handle them automatically
    sed -i.bak 's/CONFIG_SYSROOT=.*/CONFIG_SYSROOT=""/' .config
    sed -i.bak 's/CONFIG_CROSS_COMPILER_PREFIX=.*/CONFIG_CROSS_COMPILER_PREFIX=""/' .config
    
    # Re-run oldconfig to apply the static settings and flags
    yes "" | make -C "$BUSYBOX_SRC" O="$build_dir" oldconfig
    
    # 3. Compile
    echo "Running make for ${arch}..."
    make CC="$cc" AR="$AR" NM="$NM" RANLIB="$RANLIB" STRIP="$STRIP" HOSTCC="clang" LDFLAGS="-static" -j"$(sysctl -n hw.ncpu)"

    # 4. Copy binary
    if [ -f "busybox" ]; then
        cp busybox "$output_bin"
        "$STRIP" "$output_bin"
        echo "Successfully built and stripped ${output_bin}"
    else
        echo "ERROR: busybox binary not found in $build_dir"
        exit 1
    fi
}

# Build both targets
build_busybox "aarch64" "$CC_ARM64" "$BUILD_DIR_ARM64"
build_busybox "x86_64" "$CC_X86_64" "$BUILD_DIR_X86_64"

echo "=== BusyBox build completed successfully ==="
