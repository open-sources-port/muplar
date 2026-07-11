#!/bin/bash
# Local lint, formatting, and static analysis checks.
#
# Copyright 2026 muplar contributors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

echo "=== Running Local Lint & Analysis Checks ==="

# Helper to check tool existence
has_cmd() {
    command -v "$1" >/dev/null 2>&1
}

# 1. Trailing Newline Check (pure bash, no tools needed)
echo -n "Checking trailing newlines... "
find cli platform tools -type f \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" -o -name "*.sh" -o -name "*.py" \) -exec sh -c '
    for f; do
        if [ -s "$f" ] && [ "$(tail -c 1 "$f" | wc -l)" -eq 0 ]; then
            echo ""
            echo "FAIL: $f does not end with a newline"
            exit 1
        fi
    done
' sh {} +
echo "OK"

# 2. Clang-format Check
if has_cmd clang-format; then
    echo -n "Checking C/C++/ObjC formatting... "
    find cli platform -type f \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" -o -name "*.m" -o -name "*.mm" \) -exec clang-format --dry-run --Werror {} +
    echo "OK"
else
    echo "WARNING: clang-format is not installed. Skipping formatting check. Install with: brew install clang-format"
fi

# 3. Shellcheck Check
if has_cmd shellcheck; then
    echo -n "Running shellcheck... "
    grep -l -E '^#!.*\b(bash|sh|dash|ksh)$' tools/*.sh | xargs shellcheck
    echo "OK"
else
    echo "WARNING: shellcheck is not installed. Skipping shell script linting. Install with: brew install shellcheck"
fi

# 4. Cppcheck Check
if has_cmd cppcheck; then
    echo "Running cppcheck..."
    cppcheck --error-exitcode=1 --enable=warning,style,performance,portability --inconclusive --library=posix \
        --suppress=missingIncludeSystem --suppress=unusedFunction --suppress=unmatchedSuppression \
        --suppress=uninitMemberVarNoCtor --suppress=noCopyConstructor --suppress=noOperatorEq \
        --suppress=cstyleCast --suppress=shadowFunction --suppress=passedByValue \
        --suppress=redundantCopyLocalConst --suppress=usleepCalled --suppress=variableScope \
        --suppress=constVariableReference --suppress=constVariablePointer --suppress=useStlAlgorithm \
        --suppress=funcArgNamesDifferent --suppress=funcArgNamesDifferentUnnamed --suppress=functionStatic \
        --suppress=functionConst --suppress=badBitmaskCheck --suppress=uselessCallsSubstr --suppress=unusedStructMember \
        --suppress=staticFunction --suppress=unusedPrivateFunction --suppress=knownConditionTrueFalse --suppress=unreadVariable \
        cli platform
    echo "cppcheck: OK"
else
    echo "WARNING: cppcheck is not installed. Skipping static analysis. Install with: brew install cppcheck"
fi

# Optional compilation build check
if [[ "${1:-}" == "--build" ]]; then
    echo "=== Running Local Build Check ==="
    for cmd in cmake ninja; do
        if ! has_cmd "$cmd"; then
            echo "ERROR: $cmd is not installed. Install via Homebrew: brew install cmake ninja"
            exit 1
        fi
    done
    ./tools/configure-build.sh
    ninja -C build elfuse_external
    ninja -C build wawona_external
    cmake --build build
    echo "build: OK"
fi

echo "=== Local Checks Completed! ==="
