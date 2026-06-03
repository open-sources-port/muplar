#!/bin/zsh
set -e

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
WINE_SRC_DIR="${ROOT_DIR}/third_party/wine"
WINE_BUILD_DIR="${BUILD_DIR}/wine-build"
WINE_PREFIX_DIR="${BUILD_DIR}/wine-prefix"
WINE_EXE="${WINE_PREFIX_DIR}/bin/wine"

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

export PATH=/usr/local/opt/bison/bin:/usr/local/opt/flex/bin:/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/usr/sbin:/sbin

HOMEBREW_MAC_PKGCONFIG_DIR="$(dirname "$(find /usr/local/Homebrew/Library/Homebrew/os/mac/pkgconfig -name bzip2.pc | head -n 1)")"
export PKG_CONFIG_LIBDIR="${HOMEBREW_MAC_PKGCONFIG_DIR}:/usr/local/opt/freetype/lib/pkgconfig:/usr/local/opt/libpng/lib/pkgconfig:/usr/local/opt/zlib/lib/pkgconfig:/usr/local/opt/sdl2/lib/pkgconfig:/usr/local/opt/gnutls/lib/pkgconfig:/usr/local/lib/pkgconfig:/usr/local/share/pkgconfig"
unset PKG_CONFIG_PATH

export CPPFLAGS="-I/usr/local/opt/freetype/include -I/usr/local/opt/libpng/include -I/usr/local/opt/zlib/include -I/usr/local/opt/bzip2/include"
export LDFLAGS="-L/usr/local/opt/freetype/lib -L/usr/local/opt/libpng/lib -L/usr/local/opt/zlib/lib -L/usr/local/opt/bzip2/lib"

export CC="clang -arch x86_64"
export CXX="clang++ -arch x86_64"

export FREETYPE_CFLAGS="$(pkg-config --cflags freetype2)"
export FREETYPE_LIBS="$(pkg-config --libs freetype2)"

mkdir -p "$WINE_BUILD_DIR"

echo "== build_wine: checking environment =="
uname -m
which pkg-config
pkg-config --cflags freetype2
pkg-config --libs freetype2

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
    --disable-tests

echo "== build_wine: running make =="
make -j"$(sysctl -n hw.logicalcpu)"

echo "== build_wine: running make install =="
make install
