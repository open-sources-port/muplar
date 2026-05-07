#!/usr/bin/env bash

# Run cppcheck static analysis on elfuse host source files (src/ only).
# Tests use raw-syscall stubs and are excluded.
#
# CI mode: --max-configs=1 + --enable=warning for speed. Generated headers
# under build/ that the source #includes must exist before invocation:
#   build/dispatch.h    -- via scripts/gen-syscall-dispatch.py
#   build/version.h     -- one-line #define
#   build/shim_blob.h   -- empty stub (the byte array contents are opaque
#                          to cppcheck and produce no useful findings)
#
# Generating real dispatch.h instead of stubbing it keeps cppcheck honest
# about the syscall dispatch layer. Stubbing shim_blob.h is acceptable
# because the file is just a byte array with no callable surface.

set -e -u -o pipefail

mapfile -d '' SOURCES < <(git ls-files -z -- 'src/*.c' 'src/**/*.c')

if [ ${#SOURCES[@]} -eq 0 ]; then
    echo "No tracked C source files found."
    exit 0
fi

BUILD_DIR=$(mktemp -d)
trap 'rm -rf "$BUILD_DIR"' EXIT

python3 scripts/gen-syscall-dispatch.py --output "$BUILD_DIR/dispatch.h"
printf '#define ELFUSE_VERSION "ci"\n' > "$BUILD_DIR/version.h"
# shim_blob.h declares an opaque byte array and its length; cppcheck
# gains nothing from the real contents, so a minimal stub suffices.
cat > "$BUILD_DIR/shim_blob.h" << 'EOF'
static const unsigned char shim_bin[1] = {0};
static const unsigned int shim_bin_len = 1;
EOF

# 120s is generous -- this should finish well below that with --max-configs=1.
timeout 120 cppcheck \
    -I. -Isrc -I"$BUILD_DIR" \
    --platform=unix64 \
    --enable=warning \
    --max-configs=1 --error-exitcode=1 --inline-suppr \
    --suppress=checkersReport --suppress=unmatchedSuppression \
    --suppress=missingIncludeSystem --suppress=noValidConfiguration \
    --suppress=normalCheckLevelMaxBranches \
    --suppress=preprocessorErrorDirective \
    --suppress=missingInclude \
    -D_GNU_SOURCE -D__APPLE__ -D__aarch64__ \
    "${SOURCES[@]}"
