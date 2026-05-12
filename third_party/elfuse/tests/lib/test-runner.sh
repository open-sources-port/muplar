# Shared shell test runner helpers for elfuse integration suites.
#
# Copyright 2026 elfuse contributors
# Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
# SPDX-License-Identifier: Apache-2.0
#
# shellcheck shell=bash
# shellcheck disable=SC2034

: "${TEST_LABEL_WIDTH:=14}"
: "${TEST_TIMEOUT:=10}"

if [ -t 1 ]; then
    # Use ANSI-C quoting so the variables hold real ESC bytes, not the literal
    # 4-char "\033" sequence. Without this, callers that pass colors as printf
    # %s arguments (e.g. tests/test-busybox.sh) emit the escape sequence as
    # plain text instead of activating the color.
    GREEN=$'\033[0;32m'
    RED=$'\033[0;31m'
    YELLOW=$'\033[1;33m'
    BLUE=$'\033[0;34m'
    RESET=$'\033[0m'
else
    GREEN=''
    RED=''
    YELLOW=''
    BLUE=''
    RESET=''
fi

pass=0
fail=0
skip=0
expected_fail=0

test_label()
{
    printf "%-${TEST_LABEL_WIDTH}s" "$1"
}

test_report()
{
    local state="$1"
    local label="$2"
    local detail="${3:-}"
    local name
    name=$(test_label "$label")

    case "$state" in
        ok) printf "%s [ ${GREEN}OK${RESET} ]%s\n" "$name" "$detail" ;;
        fail) printf "%s [ ${RED}FAIL${RESET} ]%s\n" "$name" "$detail" ;;
        skip) printf "%s [ ${YELLOW}SKIP${RESET} ]%s\n" "$name" "$detail" ;;
        xfail) printf "%s [ ${YELLOW}XFAIL${RESET} ]%s\n" "$name" "$detail" ;;
    esac
}

test_excerpt()
{
    local output="$1"
    printf "  %.120s\n" "$output" | head -3
}

test_tool_path()
{
    printf "%s/%s" "$BIN" "$1"
}

test_skip_missing_tool()
{
    local tool="$1"
    if [ "${TEST_SKIP_MISSING_TOOLS:-0}" = "1" ] && [ ! -e "$(test_tool_path "$tool")" ]; then
        run_skip "$tool" "binary not found"
        return 0
    fi
    return 1
}

run()
{
    local tool="$1"
    shift
    local expect_rc="${1:-0}"
    shift || true
    local output rc detail

    if test_skip_missing_tool "$tool"; then
        return
    fi

    if output=$("${TEST_RUNNER[@]}" "$(test_tool_path "$tool")" "$@" 2>&1); then
        rc=0
    else
        rc=$?
    fi

    if [ "$rc" = "$expect_rc" ]; then
        detail=""
        [ "$expect_rc" -ne 0 ] && detail=" (exit $rc)"
        test_report ok "$tool" "$detail"
        pass=$((pass + 1))
    else
        test_report fail "$tool" " (got $rc, expected $expect_rc)"
        test_excerpt "$output"
        fail=$((fail + 1))
    fi
}

run_check()
{
    local tool="$1"
    shift
    local pattern="$1"
    shift
    local output rc

    if test_skip_missing_tool "$tool"; then
        return
    fi

    if output=$("${TEST_RUNNER[@]}" "$(test_tool_path "$tool")" "$@" 2>&1); then
        rc=0
    else
        rc=$?
    fi

    if [ "$rc" -ne 0 ]; then
        test_report fail "$tool" " (exit rc=$rc)"
        test_excerpt "$output"
        fail=$((fail + 1))
    elif printf "%s\n" "$output" | grep -qE "$pattern"; then
        test_report ok "$tool"
        pass=$((pass + 1))
    else
        test_report fail "$tool" " (pattern '$pattern' not found, rc=$rc)"
        test_excerpt "$output"
        fail=$((fail + 1))
    fi
}

run_xfail()
{
    local tool="$1"
    shift
    local reason="$1"
    test_report xfail "$tool" " ($reason)"
    expected_fail=$((expected_fail + 1))
}

run_skip()
{
    local tool="$1"
    shift
    local reason="$1"
    test_report skip "$tool" " ($reason)"
    skip=$((skip + 1))
}

run_pipe()
{
    local tool="$1"
    shift
    local pattern="$1"
    shift
    local input="${1:-}"
    shift || true
    local output rc

    if test_skip_missing_tool "$tool"; then
        return
    fi

    if output=$(printf '%s' "$input" \
        | timeout "$TEST_TIMEOUT" "${TEST_RUNNER[@]}" "$(test_tool_path "$tool")" "$@" 2>&1); then
        rc=0
    else
        rc=$?
    fi

    if [ "$rc" -ne 0 ]; then
        test_report fail "$tool" " (exit rc=$rc)"
        test_excerpt "$output"
        fail=$((fail + 1))
    elif printf "%s\n" "$output" | grep -qE "$pattern"; then
        test_report ok "$tool"
        pass=$((pass + 1))
    else
        test_report fail "$tool" " (pattern '$pattern' not found, rc=$rc)"
        test_excerpt "$output"
        fail=$((fail + 1))
    fi
}

run_timeout()
{
    local secs="$1"
    shift
    local tool="$1"
    shift
    local expect_rc="${1:-0}"
    shift || true
    local output rc

    if test_skip_missing_tool "$tool"; then
        return
    fi

    if output=$(timeout "$secs" "${TEST_RUNNER[@]}" "$(test_tool_path "$tool")" "$@" 2>&1); then
        rc=0
    else
        rc=$?
    fi

    if [ "$rc" = "$expect_rc" ]; then
        test_report ok "$tool"
        pass=$((pass + 1))
    else
        test_report fail "$tool" " (got $rc, expected $expect_rc)"
        test_excerpt "$output"
        fail=$((fail + 1))
    fi
}
