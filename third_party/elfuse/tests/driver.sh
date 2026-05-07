#!/usr/bin/env bash
# driver.sh - Data-driven test driver for elfuse
#
# Copyright 2026 elfuse contributors
# Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
# SPDX-License-Identifier: Apache-2.0
#
# Usage: tests/driver.sh [options] [test-name ...]
#
# Options:
#   -e ELFUSE    Path to elfuse binary (default: build/elfuse)
#   -d TESTDIR   Directory containing guest test binaries (default: build)
#   -t TIMEOUT   Per-test timeout in seconds (default: 60)
#   -f FILTER    Run only tests matching this grep pattern
#   -l           List available tests without running them
#   -v           Verbose: show test stdout/stderr on failure
#   -T           TAP output format
# If test-name arguments are given, only those tests run.
# The test list is read from tests/manifest.txt.

set -uo pipefail

ELFUSE="${ELFUSE:-build/elfuse}"
TESTDIR="${TESTDIR:-build}"
TIMEOUT=60
FILTER=""
LIST_ONLY=0
VERBOSE=0
TAP=0

usage()
{
    echo "Usage: $0 [-e elfuse] [-d testdir] [-t timeout] [-f filter] [-l] [-v] [-T] [test ...]" >&2
}

while [ $# -gt 0 ]; do
    case "$1" in
        -e)
            ELFUSE="${2:?missing argument for -e}"
            shift 2
            ;;
        -d)
            TESTDIR="${2:?missing argument for -d}"
            shift 2
            ;;
        -t)
            TIMEOUT="${2:?missing argument for -t}"
            shift 2
            ;;
        -f)
            FILTER="${2:?missing argument for -f}"
            shift 2
            ;;
        -l)
            LIST_ONLY=1
            shift
            ;;
        -v)
            VERBOSE=1
            shift
            ;;
        -T)
            TAP=1
            shift
            ;;
        --)
            shift
            break
            ;;
        -*)
            usage
            exit 1
            ;;
        *)
            break
            ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TEST_LIST="$SCRIPT_DIR/manifest.txt"

case "$ELFUSE" in
    /*) ;;
    *) ELFUSE="$REPO_ROOT/$ELFUSE" ;;
esac

case "$TESTDIR" in
    /*) TESTDIR_ABS="$TESTDIR" ;;
    *) TESTDIR_ABS="$REPO_ROOT/$TESTDIR" ;;
esac

if [ ! -f "$TEST_LIST" ]; then
    echo "error: $TEST_LIST not found" >&2
    exit 1
fi

if [ ! -x "$ELFUSE" ] && [ "$LIST_ONLY" -eq 0 ]; then
    echo "error: $ELFUSE not found or not executable" >&2
    echo "  Build with: make elfuse" >&2
    exit 1
fi

if [ "$TAP" -eq 1 ] || [ ! -t 1 ]; then
    GREEN="" RED="" YELLOW="" RESET=""
else
    GREEN='\033[32m'
    RED='\033[31m'
    YELLOW='\033[1;33m'
    RESET='\033[0m'
fi

declare -a test_names=()
declare -a test_cmds=()
declare -a test_expected=()
declare -a test_stdout=()
declare -a test_sections=()
current_section="tests"

while IFS= read -r line; do
    [[ "$line" =~ ^[[:space:]]*$ ]] && continue
    [[ "$line" =~ ^[[:space:]]*# ]] && [[ ! "$line" =~ ^\[section\] ]] && continue

    if [[ "$line" =~ ^\[section\][[:space:]]+(.+) ]]; then
        current_section="${BASH_REMATCH[1]}"
        continue
    fi

    expected=""
    stdout_pat=""
    if [[ "$line" =~ \#[^#]*expected_rc=([0-9]+) ]]; then
        expected="${BASH_REMATCH[1]}"
    fi
    if [[ "$line" =~ \#[^#]*stdout=([^[:space:]]+) ]]; then
        stdout_pat="${BASH_REMATCH[1]}"
    fi
    line="${line%%#*}"
    line="$(printf "%s" "$line" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
    [ -z "$line" ] && continue

    read -r binary _rest <<< "$line"
    name="$(basename "$binary")"

    test_names+=("$name")
    test_cmds+=("$line")
    test_expected+=("$expected")
    test_stdout+=("$stdout_pat")
    test_sections+=("$current_section")
done < "$TEST_LIST"

declare -a filtered_idx=()
if [ $# -gt 0 ]; then
    for arg in "$@"; do
        for i in "${!test_names[@]}"; do
            if [ "${test_names[$i]}" = "$arg" ]; then
                filtered_idx+=("$i")
            fi
        done
    done
    if [ ${#filtered_idx[@]} -eq 0 ]; then
        echo "error: no tests matched: $*" >&2
        exit 1
    fi
elif [ -n "$FILTER" ]; then
    for i in "${!test_names[@]}"; do
        if printf "%s\n" "${test_names[$i]}" | grep -q "$FILTER"; then
            filtered_idx+=("$i")
        fi
    done
    if [ ${#filtered_idx[@]} -eq 0 ]; then
        echo "error: no tests matched filter: $FILTER" >&2
        exit 1
    fi
else
    for i in "${!test_names[@]}"; do
        filtered_idx+=("$i")
    done
fi

if [ "$LIST_ONLY" -eq 1 ]; then
    prev_section=""
    for i in "${filtered_idx[@]}"; do
        if [ "${test_sections[$i]}" != "$prev_section" ]; then
            printf "%s\n" "${test_sections[$i]}"
            prev_section="${test_sections[$i]}"
        fi
        printf "  %s" "${test_names[$i]}"
        [ -n "${test_expected[$i]}" ] && printf " (expect rc=%s)" "${test_expected[$i]}"
        [ -n "${test_stdout[$i]}" ] && printf " (stdout=%s)" "${test_stdout[$i]}"
        printf "\n"
    done
    exit 0
fi

evaluate_result()
{
    local rc="$1"
    local expected="$2"
    local stdout_pat="$3"
    local output="$4"

    if [ "$rc" -eq 124 ]; then
        return 1
    fi
    if [ "$rc" -eq 0 ] || {
        [ -n "$expected" ] && [ "$rc" -eq "$expected" ]
    }; then
        if [ -n "$stdout_pat" ] && ! grep -qE "$stdout_pat" <<< "$output"; then
            return 1
        fi
        return 0
    fi
    return 1
}

report_case()
{
    local state="$1"
    local name="$2"
    local detail="$3"

    case "$state" in
        ok) printf "%-45s [ ${GREEN}OK${RESET} ]%s\n" "$name" "$detail" ;;
        fail) printf "%-45s [ ${RED}FAIL${RESET} ]%s\n" "$name" "$detail" ;;
        skip) printf "%-45s [ ${YELLOW}SKIP${RESET} ]%s\n" "$name" "$detail" ;;
    esac
}

print_output_excerpt()
{
    local output="$1"
    local lines="$2"

    [ "$VERBOSE" -eq 1 ] && [ -n "$output" ] || return 0
    printf "%s\n" "$output" | head -"$lines" | sed 's/^/    /'
}

total=${#filtered_idx[@]}
pass=0
fail=0
skip=0
test_num=0

if [ "$TAP" -eq 1 ]; then
    echo "TAP version 14"
    echo "1..$total"
fi

prev_section=""

for i in "${filtered_idx[@]}"; do
    test_num=$((test_num + 1))
    name="${test_names[$i]}"
    cmd_line="${test_cmds[$i]}"
    expected="${test_expected[$i]}"
    stdout_pat="${test_stdout[$i]}"
    section="${test_sections[$i]}"

    read -r -a argv <<< "$cmd_line"
    binary="${argv[0]}"
    unset 'argv[0]'
    if [[ "$binary" != /* ]]; then
        binary="$TESTDIR_ABS/$binary"
    fi

    args=()
    for arg in "${argv[@]}"; do
        arg="${arg//\$TESTDIR/$TESTDIR_ABS}"
        args+=("$arg")
    done

    if [ ! -f "$binary" ]; then
        if [ "$TAP" -eq 1 ]; then
            echo "ok $test_num - $name # SKIP binary not found"
        else
            if [ "$section" != "$prev_section" ]; then
                printf "%s\n" "$section"
                prev_section="$section"
            fi
            report_case skip "$name" ""
        fi
        skip=$((skip + 1))
        continue
    fi

    if [ "$TAP" -eq 0 ] && [ "$section" != "$prev_section" ]; then
        printf "%s\n" "$section"
        prev_section="$section"
    fi

    output=""
    if output=$(timeout "$TIMEOUT" "$ELFUSE" "$binary" "${args[@]}" 2>&1); then
        rc=0
    else
        rc=$?
    fi

    if evaluate_result "$rc" "$expected" "$stdout_pat" "$output"; then
        passed=1
    else
        passed=0
    fi

    if [ "$TAP" -eq 1 ]; then
        if [ "$passed" -eq 1 ]; then
            echo "ok $test_num - $name"
            pass=$((pass + 1))
        elif [ "$rc" -eq 124 ]; then
            echo "not ok $test_num - $name # timeout after ${TIMEOUT}s"
            fail=$((fail + 1))
        else
            echo "not ok $test_num - $name # exit code $rc"
            if [ "$VERBOSE" -eq 1 ] && [ -n "$output" ]; then
                printf "%s\n" "$output" | head -10 | sed 's/^/  # /'
            fi
            fail=$((fail + 1))
        fi
    else
        if [ "$passed" -eq 1 ]; then
            if [ -n "$expected" ] && [ "$rc" -ne 0 ]; then
                report_case ok "$name" " (exit $rc)"
            else
                report_case ok "$name" ""
            fi
            pass=$((pass + 1))
        elif [ "$rc" -eq 124 ]; then
            report_case fail "$name" " (timeout after ${TIMEOUT}s)"
            fail=$((fail + 1))
        else
            report_case fail "$name" " (exit $rc)"
            print_output_excerpt "$output" 8
            fail=$((fail + 1))
        fi
    fi
done

if [ "$TAP" -eq 0 ]; then
    if [ "$fail" -eq 0 ] && [ "$skip" -eq 0 ]; then
        printf "  All %d tests passed\n" "$pass"
    else
        printf "  Results: %d passed, %d failed, %d skipped (of %d)\n" \
            "$pass" "$fail" "$skip" "$total"
    fi
fi

[ "$fail" -eq 0 ]
