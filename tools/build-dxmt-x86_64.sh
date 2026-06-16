#!/bin/zsh
set -e

if [ "$(uname -m)" != "x86_64" ]; then
    echo "== build_dxmt: restarting under Rosetta x86_64 =="
    exec arch -x86_64 /bin/zsh "$0" "$@"
    echo "ERROR: failed to restart under Rosetta"
    exit 1
fi

export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/usr/sbin:/sbin

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ROOT_DIR="$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)"

: "${DXMT_SRC_DIR:=${ROOT_DIR}/third_party/dxmt}"
: "${DXMT_BUILD_DIR:=${ROOT_DIR}/build/dxmt-build}"
: "${WINE_INSTALL_PATH:=${ROOT_DIR}/build/wine-prefix}"
: "${LLVM_PATH:=/usr/local/opt/llvm@15}"

echo "== build_dxmt: DXMT_SRC_DIR=${DXMT_SRC_DIR} =="
echo "== build_dxmt: DXMT_BUILD_DIR=${DXMT_BUILD_DIR} =="
echo "== build_dxmt: Windows compatibility install path configured =="
echo "== build_dxmt: LLVM_PATH=${LLVM_PATH} =="
echo "== build_dxmt: module install prefix configured =="

DXMT_FORCE_REBUILD="${DXMT_FORCE_REBUILD:-0}"

DXMT_REQUIRED_FILES=(
    "${WINE_INSTALL_PATH}/lib/wine/x86_64-unix/winemetal.so"
    "${WINE_INSTALL_PATH}/lib/wine/x86_64-windows/winemetal.dll"
    "${WINE_INSTALL_PATH}/lib/wine/x86_64-windows/d3d11.dll"
    "${WINE_INSTALL_PATH}/lib/wine/x86_64-windows/dxgi.dll"
)

if [ "${DXMT_FORCE_REBUILD}" != "1" ]; then
    missing=0

    for file in "${DXMT_REQUIRED_FILES[@]}"; do
        if [ ! -f "${file}" ]; then
            echo "== build_dxmt: missing ${file} =="
            missing=1
        fi
    done

    if [ "${missing}" = "0" ]; then
        echo "== build_dxmt: DXMT already installed, skipping =="
        echo "== build_dxmt: set DXMT_FORCE_REBUILD=1 to rebuild =="
        exit 0
    fi
fi

if ! command -v meson >/dev/null 2>&1; then
    echo "ERROR: meson not found, run: brew install meson"
    exit 1
fi

mkdir -p "${DXMT_BUILD_DIR}"

if [ ! -f "${DXMT_BUILD_DIR}/build.ninja" ]; then
    echo "== build_dxmt: configuring =="

    meson setup "${DXMT_BUILD_DIR}" "${DXMT_SRC_DIR}" \
        --cross-file "${DXMT_SRC_DIR}/build-win64.txt" \
        --buildtype=release \
        --prefix="${WINE_INSTALL_PATH}/lib/wine" \
        -Dnative_llvm_path="${LLVM_PATH}" \
        -Dwine_install_path="${WINE_INSTALL_PATH}"
else
    echo "== build_dxmt: already configured =="
fi

echo "== build_dxmt: building =="
meson compile -C "${DXMT_BUILD_DIR}"

echo "== build_dxmt: installing =="
meson install -C "${DXMT_BUILD_DIR}"

echo "== build_dxmt: done =="
