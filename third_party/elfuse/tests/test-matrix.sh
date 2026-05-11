#!/usr/bin/env bash
# test-matrix.sh — Run aarch64 test suites under both elfuse and self-contained
# qemu-system-aarch64 reference VM.
#
# Modes:
#   elfuse-aarch64  — run binaries on macOS via build/elfuse
#   qemu-aarch64    — run binaries natively inside qemu-system-aarch64
#                     (boots an Alpine minirootfs initramfs that the
#                     fixture script downloads on demand)
#   all             — run both modes back-to-back
#
# Environment overrides (defaults point at externals/test-fixtures/):
#   GUEST_TEST_BINARIES        dir of internal test binaries (build/ by default)
#   GUEST_COREUTILS            dir of coreutils-equivalent binaries
#   GUEST_BUSYBOX              path to a single busybox binary
#   GUEST_STATIC_BINS          dir of dash/bash/lua/jq/etc. (optional)
#   GUEST_SYSROOT              musl sysroot for elfuse --sysroot dynamic mode
#   GUEST_DYNAMIC_COREUTILS    dir of dynamic coreutils binaries (musl)
#
# Copyright 2026 elfuse contributors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FIXTURES="${REPO_ROOT}/externals/test-fixtures"

MODE="${1:?Usage: $0 <elfuse-aarch64|qemu-aarch64|all>}"

# Tool paths.
ELFUSE="${ELFUSE:-${REPO_ROOT}/build/elfuse}"

# Default fixture paths.
# Each variable points at the actual directory (or file for busybox);
# no implicit /bin suffix is appended.
: "${GUEST_TEST_BINARIES:=${REPO_ROOT}/build}"
: "${GUEST_COREUTILS:=${FIXTURES}/aarch64-musl/staticbin/bin}"
: "${GUEST_BUSYBOX:=${FIXTURES}/aarch64-musl/staticbin/bin/busybox}"
: "${GUEST_STATIC_BINS:=${FIXTURES}/aarch64-musl/dyn-bin}"
: "${GUEST_SYSROOT:=${FIXTURES}/rootfs}"
: "${GUEST_DYNAMIC_COREUTILS:=${FIXTURES}/aarch64-musl/dyn-bin}"
: "${GUEST_GLIBC_SYSROOT:=}"
: "${GUEST_GLIBC_DYNAMIC_COREUTILS:=}"

# Reuse the shared per-test reporter so the output matches `make check`
# (which drives tests through tests/driver.sh). TEST_LABEL_WIDTH controls the
# left-aligned name column and must be set before the source so the helper
# library picks it up.
# shellcheck disable=SC2034  # Consumed by tests/lib/test-runner.sh.
TEST_LABEL_WIDTH=45
# shellcheck source=tests/lib/test-runner.sh
source "${REPO_ROOT}/tests/lib/test-runner.sh"

# Globals (test-runner.sh seeds pass/fail/skip; test-matrix.sh resets them
# per mode and tracks no extra counters).
pass=0
fail=0
skip=0

# Test fixture directory; for qemu mode it points at a path inside the VM.
TEST_TMPDIR=""
_qemu_active=0

ensure_fixtures()
{
    if [ ! -s "${FIXTURES}/aarch64-musl/staticbin/bin/busybox" ] \
        || [ ! -s "${FIXTURES}/initramfs.cpio.gz" ]; then
        printf "Fetching test fixtures (one-time download)\n"
        bash "${REPO_ROOT}/tests/fetch-fixtures.sh"
    fi
}

setup_fixtures()
{
    local mode="$1"
    case "$mode" in
        qemu-*)
            TEST_TMPDIR=$(qemu_exec mktemp -d /tmp/test-matrix.XXXXXX 2> /dev/null)
            qemu_exec sh -c "
            echo 'hello world' > '${TEST_TMPDIR}/hello.txt' &&
            printf 'cherry\napple\nbanana\n' > '${TEST_TMPDIR}/unsorted.txt' &&
            printf 'line1\nline2\nline3\nline4\nline5\n' > '${TEST_TMPDIR}/lines.txt'
        " 2> /dev/null
            ;;
        *)
            TEST_TMPDIR=$(mktemp -d)
            echo "hello world" > "$TEST_TMPDIR/hello.txt"
            printf 'cherry\napple\nbanana\n' > "$TEST_TMPDIR/unsorted.txt"
            printf 'line1\nline2\nline3\nline4\nline5\n' > "$TEST_TMPDIR/lines.txt"
            ;;
    esac
}

cleanup_fixtures()
{
    if [ "$_qemu_active" = "1" ] && [ -n "$TEST_TMPDIR" ]; then
        qemu_exec rm -rf "$TEST_TMPDIR" 2> /dev/null || true
    elif [ -n "$TEST_TMPDIR" ] && [ -d "$TEST_TMPDIR" ]; then
        rm -rf "$TEST_TMPDIR"
    fi
    TEST_TMPDIR=""
}

cleanup_qemu()
{
    if [ "$_qemu_active" = "1" ]; then
        qemu_stop 2> /dev/null || true
        _qemu_active=0
    fi
}

trap 'cleanup_fixtures; cleanup_qemu' EXIT

# Launch helpers.
# When the binary being launched lives under GUEST_SYSROOT (i.e., it's a
# dynamically-linked Alpine binary), pass --sysroot so the loader can find
# its libc.  Static binaries from build/ run unchanged.
run_elfuse()
{
    local first="${1:-}" args=()
    if [ -n "${GUEST_SYSROOT:-}" ]; then
        case "$first" in
            "${GUEST_SYSROOT}"/*) args+=(--sysroot "$GUEST_SYSROOT") ;;
            "${FIXTURES}/aarch64-musl/dyn-bin"/*) args+=(--sysroot "$GUEST_SYSROOT") ;;
        esac
    fi
    timeout 30 "$ELFUSE" "${args[@]}" "$@" 2> /dev/null
}

# `timeout` cannot wrap a shell function, so this runner inlines the path
# rewriting + ssh invocation that qemu_exec would otherwise do.
# Repo paths under REPO_ROOT are rewritten to /mnt/host/...; the remote
# command is launched with cwd=/mnt/host so unqualified paths in test
# arguments (e.g. "tests/hello.S") resolve against the
# 9p-shared repo just like in elfuse mode.
run_qemu()
{
    local args=() a
    for a in "$@"; do
        case "$a" in
            "${REPO_ROOT}"/*) args+=("/mnt/host/${a#${REPO_ROOT}/}") ;;
            *) args+=("$a") ;;
        esac
    done
    local quoted
    printf -v quoted '%q ' "${args[@]}"
    timeout 60 ssh \
        -o StrictHostKeyChecking=no \
        -o UserKnownHostsFile=/dev/null \
        -o LogLevel=ERROR \
        -o BatchMode=yes \
        -o ConnectTimeout=10 \
        -o ServerAliveInterval=15 \
        -o ServerAliveCountMax=4 \
        -i "$QEMU_SSH_KEY" -p "$QEMU_PORT" \
        root@127.0.0.1 "cd /mnt/host && ${quoted}" 2> /dev/null
}

# elfuse with --sysroot for dynamically-linked guest binaries.
_SYSROOT=""
_ELFUSE_TIMEOUT=30
_GUEST_EXTRA=""

# Optional suffix for run_coreutils_tests section labels so dynamic and static
# runs are distinguishable in the merged output.
_COREUTILS_SUFFIX=""

run_elfuse_sysroot()
{
    local bin="$1"
    shift
    local sysroot_args=""
    [ -n "$_SYSROOT" ] && sysroot_args="--sysroot $_SYSROOT"
    # shellcheck disable=SC2086
    timeout "$_ELFUSE_TIMEOUT" "$ELFUSE" $sysroot_args "$bin" $_GUEST_EXTRA "$@" 2> /dev/null
}

# Generic test helpers.

# Tests that either hang under qemu-system-aarch64 on Apple Silicon
# (raw clone / PI futex / massive thread+mmap stress) or currently diverge
# from the Alpine linux-virt reference kernel on the deprecated oom_adj
# procfs compatibility path exercised by test-io-opt. test-sysfs-cpu asserts
# the elfuse stub contract (cache/topology subtree empty, possible == online,
# cpuN count == online count) which a real kernel does not honor. All listed
# tests still run in elfuse-aarch64 mode and in `make check`; the qemu
# reference run skips them.
QEMU_SKIP="test-thread test-stress test-futex-pi test-io-opt test-sysfs-cpu"

is_qemu_skipped()
{
    local label="$1"
    case " $QEMU_SKIP " in
        *" $label "*) return 0 ;;
        *) return 1 ;;
    esac
}

# Honor QEMU_SKIP across all test_* wrappers.  Returns 0 (and prints SKIP)
# if the caller should not run the test; non-zero means proceed.
maybe_qemu_skip()
{
    local runner="$1" label="$2"
    if [ "$runner" = "run_qemu" ] && is_qemu_skipped "$label"; then
        test_report skip "$label" " (qemu)"
        skip=$((skip + 1))
        return 0
    fi
    return 1
}

# Report a timeout as a failure, matching tests/driver.sh.
report_timeout()
{
    local label="$1"
    test_report fail "$label" " (timeout)"
    fail=$((fail + 1))
}

test_check()
{
    local runner="$1"
    shift
    local label="$1"
    shift
    local pattern="$1"
    shift

    maybe_qemu_skip "$runner" "$label" && return

    local output rc
    if output=$($runner "$@"); then rc=0; else rc=$?; fi

    if [ "$rc" = "124" ]; then
        report_timeout "$label"
        return
    fi
    if echo "$output" | grep -qE "$pattern"; then
        test_report ok "$label"
        pass=$((pass + 1))
    else
        test_report fail "$label" " (exit $rc)"
        test_excerpt "$output"
        fail=$((fail + 1))
    fi
}

test_rc()
{
    local runner="$1"
    shift
    local label="$1"
    shift
    local expect_rc="$1"
    shift

    maybe_qemu_skip "$runner" "$label" && return

    local output rc
    if output=$($runner "$@"); then rc=0; else rc=$?; fi

    if [ "$rc" = "124" ]; then
        report_timeout "$label"
        return
    fi
    if [ "$rc" = "$expect_rc" ]; then
        local detail=""
        [ "$expect_rc" -ne 0 ] && detail=" (exit $rc)"
        test_report ok "$label" "$detail"
        pass=$((pass + 1))
    else
        test_report fail "$label" " (got $rc, expected $expect_rc)"
        test_excerpt "$output"
        fail=$((fail + 1))
    fi
}

test_pipe()
{
    local runner="$1"
    shift
    local label="$1"
    shift
    local pattern="$1"
    shift
    local input="$1"
    shift

    maybe_qemu_skip "$runner" "$label" && return

    local output rc
    if output=$(printf '%s' "$input" | $runner "$@"); then rc=0; else rc=$?; fi

    if [ "$rc" = "124" ]; then
        report_timeout "$label"
        return
    fi
    if echo "$output" | grep -qE "$pattern"; then
        test_report ok "$label"
        pass=$((pass + 1))
    else
        test_report fail "$label" " (exit $rc)"
        test_excerpt "$output"
        fail=$((fail + 1))
    fi
}

# Test suites.
run_unit_tests()
{
    local runner="$1" bindir="$2"

    printf "Assembly tests\n"
    test_check "$runner" "test-hello" "hello" "$bindir/test-hello"

    printf "\nC unit tests\n"
    test_check "$runner" "hello-musl" "Hello" "$bindir/hello-musl"
    test_check "$runner" "hello-write" "Hello" "$bindir/hello-write"
    test_check "$runner" "echo-test" "hello world" "$bindir/echo-test" hello world
    test_check "$runner" "test-argc" "argc.*3" "$bindir/test-argc" arg1 arg2
    test_rc "$runner" "test-complex" 42 "$bindir/test-complex"
    test_check "$runner" "test-fileio" "lines" "$bindir/test-fileio" LICENSE
    test_check "$runner" "test-string" "memcpy" "$bindir/test-string"
    test_check "$runner" "test-malloc" "OK" "$bindir/test-malloc"
    test_check "$runner" "test-cat" "" "$bindir/test-cat" tests/hello.S
    test_check "$runner" "test-ls" "hello" "$bindir/test-ls" tests/
    test_check "$runner" "test-roundtrip" "OK" "$bindir/test-roundtrip"
    test_check "$runner" "test-comprehensive" "0 failures" "$bindir/test-comprehensive"

    printf "\nProcess tests\n"
    test_check "$runner" "test-fork" "PASS" "$bindir/test-fork"
    test_check "$runner" "test-exec" "exec-works" "$bindir/test-exec" "$bindir/echo-test" exec-works
    test_check "$runner" "test-fork-exec" "PASS" "$bindir/test-fork-exec" "$bindir/echo-test"
    test_check "$runner" "test-cloexec" "PASS" "$bindir/test-cloexec"

    printf "\nSignal tests\n"
    test_check "$runner" "test-signal" "PASS|0 failed" "$bindir/test-signal"
    test_check "$runner" "test-signal-thread" "PASS|0 failed" "$bindir/test-signal-thread"
    test_check "$runner" "test-sigill" "0 failed" "$bindir/test-sigill"

    printf "\nSocket tests\n"
    test_check "$runner" "test-socket" "PASS|0 failed" "$bindir/test-socket"

    printf "\nSyscall coverage\n"
    test_check "$runner" "test-file-ops" "0 failed" "$bindir/test-file-ops"
    test_check "$runner" "test-sysinfo" "0 failed" "$bindir/test-sysinfo"
    test_check "$runner" "test-io-opt" "0 failed" "$bindir/test-io-opt"
    test_check "$runner" "test-poll" "0 failed" "$bindir/test-poll"

    printf "\nI/O subsystem\n"
    test_check "$runner" "test-eventfd" "0 failed" "$bindir/test-eventfd"
    test_check "$runner" "test-signalfd" "0 failed" "$bindir/test-signalfd"
    test_check "$runner" "test-signalfd-hardening" "0 failed" \
        "$bindir/test-signalfd-hardening"
    test_check "$runner" "test-epoll" "0 failed" "$bindir/test-epoll"
    test_check "$runner" "test-epoll-edge" "0 failed" "$bindir/test-epoll-edge"
    test_check "$runner" "test-timerfd" "0 failed" "$bindir/test-timerfd"

    printf "\n/proc and /dev\n"
    test_check "$runner" "test-proc" "0 failed" "$bindir/test-proc"
    test_check "$runner" "test-sysfs-cpu" "0 failed" "$bindir/test-sysfs-cpu"

    printf "\nNetwork\n"
    test_check "$runner" "test-net" "0 failed" "$bindir/test-net"

    printf "\nThreading\n"
    test_check "$runner" "test-thread" "0 failed" "$bindir/test-thread"
    test_check "$runner" "test-pthread" "0 failed" "$bindir/test-pthread"
    test_check "$runner" "test-simd-clone" "0 failed" "$bindir/test-simd-clone"
    test_check "$runner" "test-stress" "0 failed" "$bindir/test-stress"

    printf "\nNegative tests\n"
    test_check "$runner" "test-negative" "0 failed" "$bindir/test-negative"

    printf "\nCOW fork isolation\n"
    test_check "$runner" "test-cow-fork" "PASS" "$bindir/test-cow-fork"

    printf "\nGuard page / mmap edge cases\n"
    test_check "$runner" "test-guard-page" "PASS" "$bindir/test-guard-page"

    printf "\nScatter-gather I/O\n"
    test_check "$runner" "test-readv-writev" "PASS" "$bindir/test-readv-writev"

    printf "\ninotify emulation\n"
    test_check "$runner" "test-inotify" "PASS" "$bindir/test-inotify"

    printf "\nPI futex + EINTR regression\n"
    test_check "$runner" "test-futex-pi" "0 failed" "$bindir/test-futex-pi"

    printf "\nX11 raw protocol\n"
    test_check "$runner" "test-x11" "0 failed" "$bindir/test-x11"
}

run_coreutils_tests()
{
    local runner="$1" bindir="$2"

    printf "Coreutils text%s\n" "$_COREUTILS_SUFFIX"
    test_check "$runner" "echo" "hello" "$bindir/echo" hello
    test_check "$runner" "cat" "hello world" "$bindir/cat" "$TEST_TMPDIR/hello.txt"
    test_check "$runner" "head" "line1" "$bindir/head" "$TEST_TMPDIR/lines.txt"
    test_check "$runner" "tail" "line5" "$bindir/tail" "$TEST_TMPDIR/lines.txt"
    test_check "$runner" "wc" "5" "$bindir/wc" -l "$TEST_TMPDIR/lines.txt"
    test_check "$runner" "sort" "apple" "$bindir/sort" "$TEST_TMPDIR/unsorted.txt"
    test_pipe "$runner" "tr" "HELLO" "hello" "$bindir/tr" a-z A-Z
    test_check "$runner" "seq" "5" "$bindir/seq" 1 5
    test_check "$runner" "expr" "3" "$bindir/expr" 1 + 2
    test_check "$runner" "factor" "2 2 3" "$bindir/factor" 12
    test_check "$runner" "base64" "aGVsbG8" "$bindir/base64" "$TEST_TMPDIR/hello.txt"
    test_check "$runner" "md5sum" "hello.txt" "$bindir/md5sum" "$TEST_TMPDIR/hello.txt"
    test_check "$runner" "sha256sum" "hello.txt" "$bindir/sha256sum" "$TEST_TMPDIR/hello.txt"

    printf "\nCoreutils file ops%s\n" "$_COREUTILS_SUFFIX"
    test_rc "$runner" "cp" 0 "$bindir/cp" "$TEST_TMPDIR/hello.txt" "$TEST_TMPDIR/hello-cp-$$"
    test_rc "$runner" "touch" 0 "$bindir/touch" "$TEST_TMPDIR/touched-$$"
    test_check "$runner" "ls" "hello" "$bindir/ls" "$TEST_TMPDIR"
    test_check "$runner" "stat" "File:" "$bindir/stat" "$TEST_TMPDIR/hello.txt"
    test_check "$runner" "basename" "hello.txt" "$bindir/basename" "$TEST_TMPDIR/hello.txt"
    test_check "$runner" "dirname" "$TEST_TMPDIR" "$bindir/dirname" "$TEST_TMPDIR/hello.txt"
    test_check "$runner" "realpath" "hello.txt" "$bindir/realpath" "$TEST_TMPDIR/hello.txt"
    test_check "$runner" "df" "Filesystem" "$bindir/df" "$TEST_TMPDIR"
    test_check "$runner" "du" "[0-9]" "$bindir/du" -s "$TEST_TMPDIR"

    printf "\nCoreutils sysinfo%s\n" "$_COREUTILS_SUFFIX"
    test_check "$runner" "uname" "Linux" "$bindir/uname" -s
    test_check "$runner" "date" "202" "$bindir/date" "+%Y"
    test_check "$runner" "id" "uid=" "$bindir/id"
    test_check "$runner" "printenv" "/" "$bindir/printenv" PATH
    test_check "$runner" "nproc" "[0-9]" "$bindir/nproc"

    printf "\nCoreutils process%s\n" "$_COREUTILS_SUFFIX"
    test_rc "$runner" "true" 0 "$bindir/true"
    test_rc "$runner" "false" 1 "$bindir/false"
    test_rc "$runner" "sleep" 0 "$bindir/sleep" 0
    test_rc "$runner" "env" 0 "$bindir/env" "$bindir/true"
    test_rc "$runner" "nice" 0 "$bindir/nice" "$bindir/true"
    test_rc "$runner" "nohup" 0 "$bindir/nohup" "$bindir/true"
    test_rc "$runner" "timeout" 0 "$bindir/timeout" 5 "$bindir/true"

    printf "\nCoreutils encoding%s\n" "$_COREUTILS_SUFFIX"
    if [ -e "$bindir/base32" ]; then
        test_check "$runner" "base32" "NBSWY" "$bindir/base32" "$TEST_TMPDIR/hello.txt"
    fi
    test_check "$runner" "sha1sum" "hello.txt" "$bindir/sha1sum" "$TEST_TMPDIR/hello.txt"
    test_check "$runner" "sha512sum" "hello.txt" "$bindir/sha512sum" "$TEST_TMPDIR/hello.txt"
    if [ -e "$bindir/b2sum" ]; then
        test_check "$runner" "b2sum" "hello.txt" "$bindir/b2sum" "$TEST_TMPDIR/hello.txt"
    fi
    test_check "$runner" "cksum" "hello.txt" "$bindir/cksum" "$TEST_TMPDIR/hello.txt"
    if [ -e "$bindir/numfmt" ]; then
        test_check "$runner" "numfmt" "1\\.0[kK]" "$bindir/numfmt" --to=si 1000
    fi
}

run_busybox_tests()
{
    local runner="$1" bb="$2"

    printf "Busybox core\n"
    test_check "$runner" "bb echo" "hello" "$bb" echo hello
    test_check "$runner" "bb cat" "hello world" "$bb" cat "$TEST_TMPDIR/hello.txt"
    test_check "$runner" "bb head" "line1" "$bb" head -n1 "$TEST_TMPDIR/lines.txt"
    test_check "$runner" "bb tail" "line5" "$bb" tail -n1 "$TEST_TMPDIR/lines.txt"
    test_check "$runner" "bb wc" "5" "$bb" wc -l "$TEST_TMPDIR/lines.txt"
    test_check "$runner" "bb sort" "apple" "$bb" sort "$TEST_TMPDIR/unsorted.txt"
    test_check "$runner" "bb seq" "5" "$bb" seq 1 5
    test_check "$runner" "bb expr" "3" "$bb" expr 1 + 2
    test_check "$runner" "bb factor" "2 2 3" "$bb" factor 12
    test_check "$runner" "bb base64" "aGVsbG8" "$bb" base64 "$TEST_TMPDIR/hello.txt"
    test_check "$runner" "bb md5sum" "hello.txt" "$bb" md5sum "$TEST_TMPDIR/hello.txt"
    test_check "$runner" "bb sha256sum" "hello.txt" "$bb" sha256sum "$TEST_TMPDIR/hello.txt"
    test_pipe "$runner" "bb tr" "HELLO" "hello" "$bb" tr a-z A-Z
    test_pipe "$runner" "bb sed" "HELLO" "hello" "$bb" sed 's/hello/HELLO/'
    test_pipe "$runner" "bb awk" "b" "a b" "$bb" awk '{print $2}'
    test_pipe "$runner" "bb grep" "hello" "hello" "$bb" grep hello

    printf "\nBusybox file ops\n"
    test_rc "$runner" "bb cp" 0 "$bb" cp "$TEST_TMPDIR/hello.txt" "$TEST_TMPDIR/bb-cp-$$"
    test_rc "$runner" "bb touch" 0 "$bb" touch "$TEST_TMPDIR/bb-touch-$$"
    test_check "$runner" "bb ls" "hello" "$bb" ls "$TEST_TMPDIR"
    test_check "$runner" "bb stat" "File:" "$bb" stat "$TEST_TMPDIR/hello.txt"
    test_check "$runner" "bb basename" "hello.txt" "$bb" basename "$TEST_TMPDIR/hello.txt"
    test_check "$runner" "bb dirname" "$TEST_TMPDIR" "$bb" dirname "$TEST_TMPDIR/hello.txt"
    test_check "$runner" "bb uname" "Linux" "$bb" uname -s
    test_check "$runner" "bb date" "202" "$bb" date "+%Y"
    test_check "$runner" "bb id" "uid=" "$bb" id

    printf "\nBusybox archive\n"
    test_rc "$runner" "bb gzip" 0 "$bb" gzip -kf "$TEST_TMPDIR/hello.txt"
    test_check "$runner" "bb zcat" "hello world" "$bb" zcat "$TEST_TMPDIR/hello.txt.gz"

    printf "\nBusybox shell\n"
    test_pipe "$runner" "bb ash" "hello" "" "$bb" ash -c "echo hello"
    test_pipe "$runner" "bb sh" "hello" "" "$bb" sh -c "echo hello"
}

run_static_tests()
{
    local runner="$1" bindir="$2"

    printf "Static bins\n"

    if [ -e "$bindir/dash" ]; then
        test_check "$runner" "dash echo" "hello" "$bindir/dash" -c "echo hello"
        test_check "$runner" "dash arithmetic" "2\\+3=5" "$bindir/dash" -c 'echo "2+3=$((2+3))"'
    fi
    if [ -e "$bindir/bash" ]; then
        test_check "$runner" "bash echo" "hello" "$bindir/bash" -c "echo hello"
        test_pipe "$runner" "bash subshell" "sub=25" "" "$bindir/bash" -c 'echo "sub=$(echo $((5*5)))"'
    fi
    if [ -e "$bindir/lua5.4" ]; then
        test_check "$runner" "lua hello" "Hello" "$bindir/lua5.4" -e 'print("Hello from " .. _VERSION)'
        test_check "$runner" "lua fib(30)" "832040" "$bindir/lua5.4" -e 'local function f(n) if n<2 then return n end; return f(n-1)+f(n-2) end; print(f(30))'
    elif [ -e "$bindir/lua" ]; then
        test_check "$runner" "lua hello" "Hello" "$bindir/lua" -e 'print("Hello from " .. _VERSION)'
        test_check "$runner" "lua fib(30)" "832040" "$bindir/lua" -e 'local function f(n) if n<2 then return n end; return f(n-1)+f(n-2) end; print(f(30))'
    fi
    if [ -e "$bindir/gawk" ]; then
        test_pipe "$runner" "gawk field" "world" "hello world" "$bindir/gawk" '{print $2}'
    fi
    if [ -e "$bindir/grep" ]; then
        test_pipe "$runner" "grep basic" "hello" "hello world" "$bindir/grep" hello
    fi
    if [ -e "$bindir/sed" ]; then
        test_pipe "$runner" "sed subst" "HELLO" "hello" "$bindir/sed" 's/hello/HELLO/'
    fi
    if [ -e "$bindir/jq" ]; then
        test_pipe "$runner" "jq simple" "^1$" '{"a":1}' "$bindir/jq" '.a'
        test_pipe "$runner" "jq filter" "Alice" '{"users":[{"name":"Alice","age":30},{"name":"Bob","age":25}]}' "$bindir/jq" '.users[] | select(.age > 28) | .name'
    fi
    if [ -e "$bindir/sqlite3" ]; then
        test_check "$runner" "sqlite version" "^3\\." "$bindir/sqlite3" ":memory:" "SELECT sqlite_version();"
        test_check "$runner" "sqlite arith" "^42$" "$bindir/sqlite3" ":memory:" "SELECT 6 * 7;"
    fi
    if [ -e "$bindir/tree" ]; then
        test_check "$runner" "tree" "director" "$bindir/tree" "$TEST_TMPDIR"
    fi
    if [ -e "$bindir/find" ]; then
        test_check "$runner" "find" "hello.txt" "$bindir/find" "$TEST_TMPDIR" -name "hello.txt"
    fi
    if [ -e "$bindir/diff" ]; then
        test_rc "$runner" "diff identical" 0 "$bindir/diff" "$TEST_TMPDIR/hello.txt" "$TEST_TMPDIR/hello.txt"
    fi
}

# Mode runners.
run_suite()
{
    local mode="$1"
    local runner dyn_runner

    case "$mode" in
        elfuse-aarch64)
            runner="run_elfuse"
            dyn_runner="run_elfuse_sysroot"
            ;;
        qemu-aarch64)
            # shellcheck disable=SC1091
            . "${REPO_ROOT}/tests/qemu-runner.sh"
            printf "Booting qemu-system-aarch64 (Alpine minirootfs)\n"
            qemu_start || {
                echo "qemu boot failed"
                return 1
            }
            _qemu_active=1
            runner="run_qemu"
            dyn_runner="run_qemu"
            ;;
        *)
            echo "Unknown mode: $mode"
            return 2
            ;;
    esac

    cleanup_fixtures
    setup_fixtures "$mode"

    printf "\nTesting: %s\n\n" "$mode"

    pass=0
    fail=0
    skip=0

    run_unit_tests "$runner" "$GUEST_TEST_BINARIES"
    run_coreutils_tests "$runner" "$GUEST_COREUTILS"
    run_busybox_tests "$runner" "$GUEST_BUSYBOX"

    if [ -d "$GUEST_STATIC_BINS" ]; then
        run_static_tests "$runner" "$GUEST_STATIC_BINS"
    fi

    # Dynamic-musl coreutils — elfuse needs --sysroot, qemu just runs natively.
    if [ -d "$GUEST_DYNAMIC_COREUTILS" ]; then
        if [ "$mode" = "elfuse-aarch64" ] && [ -z "$GUEST_SYSROOT" ]; then
            printf "\nDynamic coreutils (musl) — SKIP (no GUEST_SYSROOT)\n"
        else
            _COREUTILS_SUFFIX=" (musl dyn)"
            _SYSROOT="$GUEST_SYSROOT"
            run_coreutils_tests "$dyn_runner" "$GUEST_DYNAMIC_COREUTILS"
            _COREUTILS_SUFFIX=""
        fi
    fi

    if [ -n "$GUEST_GLIBC_DYNAMIC_COREUTILS" ] && [ -d "$GUEST_GLIBC_DYNAMIC_COREUTILS" ]; then
        if [ "$mode" = "elfuse-aarch64" ] && [ -z "$GUEST_GLIBC_SYSROOT" ]; then
            printf "\nDynamic coreutils (glibc) — SKIP (no GUEST_GLIBC_SYSROOT)\n"
        else
            _COREUTILS_SUFFIX=" (glibc dyn)"
            _SYSROOT="$GUEST_GLIBC_SYSROOT"
            run_coreutils_tests "$dyn_runner" "$GUEST_GLIBC_DYNAMIC_COREUTILS"
            _COREUTILS_SUFFIX=""
        fi
    fi

    _SYSROOT=""

    local total=$((pass + fail + skip))
    if [ "$fail" -eq 0 ] && [ "$skip" -eq 0 ]; then
        printf "  All %d tests passed\n\n" "$pass"
    else
        printf "  Results: %d passed, %d failed, %d skipped (of %d)\n\n" \
            "$pass" "$fail" "$skip" "$total"
    fi

    cleanup_fixtures
    cleanup_qemu

    return "$fail"
}

# Main entry point.
ensure_fixtures

total_fail=0
if [ "$MODE" = "all" ]; then
    for m in elfuse-aarch64 qemu-aarch64; do
        run_suite "$m" || total_fail=$((total_fail + $?))
    done
    exit "$total_fail"
else
    run_suite "$MODE"
fi
