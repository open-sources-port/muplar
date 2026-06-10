#!/bin/zsh
set -e

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
WINE_SRC_DIR="${ROOT_DIR}/third_party/wine"
WINE_BUILD_DIR="${BUILD_DIR}/wine-build"
WINE_PREFIX_DIR="${BUILD_DIR}/wine-prefix"
WINE_EXE="${WINE_PREFIX_DIR}/bin/wine"

BREW_PREFIX="/usr/local"

if [ -x "${WINE_EXE}" ]; then
    echo "== build_wine: Wine already installed, skipping =="
    exit 0
fi

if [ "$(uname -m)" != "x86_64" ]; then
    echo "== build_wine: restarting under Rosetta x86_64 =="
    exec arch -x86_64 /bin/zsh "$0" "$@"
    echo "ERROR: failed to restart under Rosetta"
    exit 1
fi

# export PATH=/usr/local/opt/bison/bin:/usr/local/opt/flex/bin:/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/usr/sbin:/sbin
export PATH="${BREW_PREFIX}/opt/bison/bin:${BREW_PREFIX}/opt/flex/bin:${BREW_PREFIX}/bin:${BREW_PREFIX}/sbin:/usr/bin:/bin:/usr/sbin:/sbin"

# Required once before running this script:
# arch -x86_64 /usr/local/bin/brew install \
#   bison flex freetype libpng zlib bzip2 sdl2 gnutls \
#   vulkan-headers vulkan-loader molten-vk vulkan-tools
HOMEBREW_MAC_PKGCONFIG_DIR="$(find "${BREW_PREFIX}/Homebrew/Library/Homebrew/os/mac/pkgconfig" -name bzip2.pc -print -quit 2>/dev/null | xargs dirname 2>/dev/null || true)"
# HOMEBREW_MAC_PKGCONFIG_DIR="$(dirname "$(find /usr/local/Homebrew/Library/Homebrew/os/mac/pkgconfig -name bzip2.pc | head -n 1)")"
export PKG_CONFIG_LIBDIR="${HOMEBREW_MAC_PKGCONFIG_DIR}:/usr/local/opt/freetype/lib/pkgconfig:/usr/local/opt/libpng/lib/pkgconfig:/usr/local/opt/zlib/lib/pkgconfig:/usr/local/opt/sdl2/lib/pkgconfig:/usr/local/opt/gnutls/lib/pkgconfig:/usr/local/lib/pkgconfig:/usr/local/share/pkgconfig"

typeset -a PKG_CONFIG_DIRS

[ -n "${HOMEBREW_MAC_PKGCONFIG_DIR}" ] && PKG_CONFIG_DIRS+=("${HOMEBREW_MAC_PKGCONFIG_DIR}")

for dir in \
    "${BREW_PREFIX}/opt/freetype/lib/pkgconfig" \
    "${BREW_PREFIX}/opt/libpng/lib/pkgconfig" \
    "${BREW_PREFIX}/opt/zlib/lib/pkgconfig" \
    "${BREW_PREFIX}/opt/bzip2/lib/pkgconfig" \
    "${BREW_PREFIX}/opt/sdl2/lib/pkgconfig" \
    "${BREW_PREFIX}/opt/gnutls/lib/pkgconfig" \
    "${BREW_PREFIX}/opt/vulkan-loader/lib/pkgconfig" \
    "${BREW_PREFIX}/opt/vulkan-headers/lib/pkgconfig" \
    "${BREW_PREFIX}/opt/molten-vk/lib/pkgconfig" \
    "${BREW_PREFIX}/lib/pkgconfig" \
    "${BREW_PREFIX}/share/pkgconfig"
do
    [ -d "$dir" ] && PKG_CONFIG_DIRS+=("$dir")
done

export PKG_CONFIG_LIBDIR="${(j/:/)PKG_CONFIG_DIRS}"
unset PKG_CONFIG_PATH

export CPPFLAGS="\
-I${BREW_PREFIX}/include \
-I${BREW_PREFIX}/opt/freetype/include \
-I${BREW_PREFIX}/opt/libpng/include \
-I${BREW_PREFIX}/opt/zlib/include \
-I${BREW_PREFIX}/opt/bzip2/include \
${CPPFLAGS:-}"

export LDFLAGS="\
-L${BREW_PREFIX}/lib \
-L${BREW_PREFIX}/opt/freetype/lib \
-L${BREW_PREFIX}/opt/libpng/lib \
-L${BREW_PREFIX}/opt/zlib/lib \
-L${BREW_PREFIX}/opt/bzip2/lib \
${LDFLAGS:-}"

export CC="clang -arch x86_64"
export CXX="clang++ -arch x86_64"

require_pkg() {
    if ! pkg-config --exists "$1"; then
        echo "ERROR: missing pkg-config package: $1"
        echo "PKG_CONFIG_LIBDIR=${PKG_CONFIG_LIBDIR}"
        echo
        echo "Try:"
        echo "  arch -x86_64 ${BREW_PREFIX}/bin/brew install vulkan-headers vulkan-loader molten-vk vulkan-tools"
        exit 1
    fi
}

require_file() {
    if [ ! -e "$1" ]; then
        echo "ERROR: missing file: $1"
        exit 1
    fi
}

echo "== build_wine: checking environment =="
uname -m
which clang
which pkg-config
pkg-config --version

require_pkg freetype2
require_pkg libpng
require_pkg zlib
require_pkg sdl2
require_pkg gnutls
require_pkg vulkan

require_file "${BREW_PREFIX}/include/vulkan/vulkan.h"
require_file "${BREW_PREFIX}/lib/libvulkan.dylib"

echo "== build_wine: freetype =="
pkg-config --cflags freetype2
pkg-config --libs freetype2

echo "== build_wine: vulkan =="
pkg-config --cflags vulkan
pkg-config --libs vulkan

export FREETYPE_CFLAGS="$(pkg-config --cflags freetype2)"
export FREETYPE_LIBS="$(pkg-config --libs freetype2)"
export VULKAN_CFLAGS="$(pkg-config --cflags vulkan)"
export VULKAN_LIBS="$(pkg-config --libs vulkan)"

mkdir -p "$WINE_BUILD_DIR"

echo "== build_wine: running configure =="
cd "$WINE_BUILD_DIR"
rm -f config.cache

"$WINE_SRC_DIR/configure" \
    --prefix="$WINE_PREFIX_DIR" \
    --enable-win64 \
    --without-x \
    --without-wayland \
    --with-freetype \
    --with-sdl \
    --with-gnutls \
    --with-vulkan \
    --disable-tests

echo "== build_wine: verifying Vulkan configure result =="
if grep -qi "vulkan.*no" config.log; then
    echo "ERROR: configure still reports Vulkan as missing"
    grep -i vulkan config.log || true
    exit 1
fi

echo "== build_wine: running make =="
make -j"$(sysctl -n hw.logicalcpu)"

echo "== build_wine: running make install =="
make install

echo "== build_wine: checking installed Vulkan Wine files =="
find "$WINE_PREFIX_DIR" -iname "vulkan-1.dll*" -o -iname "winevulkan*"

echo "== build_wine: done =="
