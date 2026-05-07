#!/usr/bin/env bash
# test-busybox.sh — Busybox 1.37.0 applet smoke tests for elfuse
#
# Copyright 2026 elfuse contributors
# Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
# SPDX-License-Identifier: Apache-2.0
#
# Tests a selection of busybox applets through elfuse. Busybox includes ~300+
# applets covering shell, networking stubs, editors, and more.
#
# Usage: tests/test-busybox.sh <elfuse-binary> <busybox-binary>

set -euo pipefail

ELFUSE="${1:?Usage: $0 <elfuse-binary> <busybox-binary>}"
BB="${2:?Usage: $0 <elfuse-binary> <busybox-binary>}"

# Use shared test runner; busybox applets are passed as arguments, not paths.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC2034  # Consumed by tests/lib/test-runner.sh.
TEST_RUNNER=("$ELFUSE" "$BB")
# shellcheck disable=SC2034  # Consumed by tests/lib/test-runner.sh.
TEST_LABEL_WIDTH=14
# shellcheck disable=SC2034  # Consumed by tests/lib/test-runner.sh.
TEST_TIMEOUT=10
# shellcheck disable=SC2034  # Consumed by tests/lib/test-runner.sh.
BIN=""
# shellcheck disable=SC1091
# shellcheck source=tests/lib/test-runner.sh
source "$SCRIPT_DIR/lib/test-runner.sh"
skip=${skip:-0}

# Override: busybox applets are arguments, not $BIN/applet paths.
# shellcheck disable=SC2329  # Invoked indirectly by tests/lib/test-runner.sh.
test_tool_path()
{
    printf "%s" "$1"
}

run_nc_http_check()
{
    local applet="nc" output rc server_pid port_file port
    port_file=$(mktemp "${TMPDIR}/nc-http-port.XXXXXX") || {
        test_report skip "$applet" " (failed to create port file)"
        skip=$((skip + 1))
        return
    }

    python3 -u -c 'import functools, http.server, socketserver, sys
handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=sys.argv[1])
with socketserver.TCPServer(("127.0.0.1", 0), handler) as server:
    print(server.server_address[1], flush=True)
    server.serve_forever()' "$TMPDIR" > "$port_file" 2> /dev/null &
    server_pid=$!

    for _ in 1 2 3 4 5 6 7 8 9 10; do
        if ! kill -0 "$server_pid" 2> /dev/null; then
            test_report skip "$applet" " (localhost HTTP server failed to start)"
            skip=$((skip + 1))
            rm -f "$port_file"
            return
        fi
        if IFS= read -r port < "$port_file" && [ -n "$port" ]; then
            break
        fi
        sleep 0.1
    done

    if [ -z "${port:-}" ]; then
        test_report skip "$applet" " (localhost HTTP server did not report a port)"
        skip=$((skip + 1))
        kill "$server_pid" 2> /dev/null || true
        wait "$server_pid" 2> /dev/null || true
        rm -f "$port_file"
        return
    fi

    if output=$( (
        printf 'HEAD / HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n'
        sleep 2
    ) \
        | timeout 10 "$ELFUSE" "$BB" nc -w 3 127.0.0.1 "$port" 2>&1); then
        rc=0
    else
        rc=$?
    fi

    kill "$server_pid" 2> /dev/null || true
    wait "$server_pid" 2> /dev/null || true
    rm -f "$port_file"

    if echo "$output" | grep -q "HTTP"; then
        test_report ok "$applet"
        pass=$((pass + 1))
        return
    fi

    test_report fail "$applet" " (pattern 'HTTP' not found, rc=$rc)"
    test_excerpt "$output"
    fail=$((fail + 1))
}

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# Test fixtures
printf 'hello world\n' > "$TMPDIR/hello.txt"
printf 'cherry\napple\nbanana\n' > "$TMPDIR/unsorted.txt"
printf 'line1\nline2\nline3\nline4\nline5\n' > "$TMPDIR/lines.txt"
mkdir -p "$TMPDIR/testdir"
printf 'content\n' > "$TMPDIR/testdir/file.txt"

printf '\n%s━━━ Busybox 1.37.0 applet smoke tests ━━━%s\n\n' "$BLUE" "$RESET"

# Core utilities
printf '%s── Core utilities ──%s\n' "$BLUE" "$RESET"
run_check echo "hello" "hello"
run_check printf "42" "%d" 42
run_check cat "hello world" "$TMPDIR/hello.txt"
run_check head "line1" "-n1" "$TMPDIR/lines.txt"
run_check tail "line5" "-n1" "$TMPDIR/lines.txt"
run_check wc "5" "-l" "$TMPDIR/lines.txt"
run_check sort "apple" "$TMPDIR/unsorted.txt"
run_check uniq "hello" "$TMPDIR/hello.txt"
run_check cut "hello" "-d " "-f1" "$TMPDIR/hello.txt"
run_pipe tr "HELLO" "hello" "a-z" "A-Z"
run_pipe sed "HELLO" "hello" "s/hello/HELLO/"
run_pipe awk "b" "a b" "{print \$2}"
run_pipe grep "hello" "hello" "hello"
run true 0
run false 1
run sleep 0 "0"

# File operations
printf '\n%s── File operations ──%s\n' "$BLUE" "$RESET"
run cp 0 "$TMPDIR/hello.txt" "$TMPDIR/hello-cp.txt"
run mv 0 "$TMPDIR/hello-cp.txt" "$TMPDIR/hello-mv.txt"
run rm 0 "$TMPDIR/hello-mv.txt"
run ln 0 "-s" "$TMPDIR/hello.txt" "$TMPDIR/bb-link.txt"
run mkdir 0 "$TMPDIR/bb-newdir"
run rmdir 0 "$TMPDIR/bb-newdir"
run touch 0 "$TMPDIR/bb-touched.txt"
run chmod 0 "644" "$TMPDIR/bb-touched.txt"
run_check ls "hello.txt" "$TMPDIR"
run_check stat "File:" "$TMPDIR/hello.txt"
run_check du "[0-9]" "-s" "$TMPDIR"
run_check df "Filesystem" "$TMPDIR"
run_check readlink "$TMPDIR/hello.txt" "$TMPDIR/bb-link.txt"
run_check realpath "" "$TMPDIR/hello.txt"
run_check basename "hello.txt" "$TMPDIR/hello.txt"
run_check dirname "$TMPDIR" "$TMPDIR/hello.txt"
run_check pwd "/"
run dd 0 "if=$TMPDIR/hello.txt" "of=$TMPDIR/bb-dd.txt" "bs=12" "count=1"
run sync 0

# Text processing
printf '\n%s── Text processing ──%s\n' "$BLUE" "$RESET"
run_check md5sum "hello.txt" "$TMPDIR/hello.txt"
run_check sha1sum "hello.txt" "$TMPDIR/hello.txt"
run_check sha256sum "hello.txt" "$TMPDIR/hello.txt"
run_check sha512sum "hello.txt" "$TMPDIR/hello.txt"
run_check od "0000000" "-c" "$TMPDIR/hello.txt"
run_check hexdump "0000000" "-C" "$TMPDIR/hello.txt"
run_check xxd "0000000" "$TMPDIR/hello.txt"
run_check base64 "aGVsbG8gd29ybGQ" "$TMPDIR/hello.txt"
run_check fold "hello" "-w5" "$TMPDIR/hello.txt"
run_check nl "hello" "$TMPDIR/hello.txt"
run_check expand "hello" "$TMPDIR/hello.txt"
run_check unexpand "hello" "$TMPDIR/hello.txt"
run_check paste "hello" "$TMPDIR/hello.txt"
run_check tac "line5" "$TMPDIR/lines.txt"
run_check rev "dlrow olleh" "$TMPDIR/hello.txt"
run_check comm "" "$TMPDIR/hello.txt" "$TMPDIR/hello.txt"

# Math / misc
printf '\n%s── Math / misc ──%s\n' "$BLUE" "$RESET"
run_check seq "5" "1" "5"
run_check expr "3" "1" "+" "2"
run_check factor "2 2 3" "12"
run_check date "" "+%Y"
run_check uname "Linux" "-s"
run_check id "uid="
run_check whoami "user"     # reads /etc/passwd (synthetic)
run_check hostname "elfuse" # returns synthetic hostname
run_check env "PATH"        # prints environment variables
run_check test "" "-f" "$TMPDIR/hello.txt"

# Proc-backed applets
printf '\n%s── Proc-backed applets ──%s\n' "$BLUE" "$RESET"
run_check ps "COMMAND"
run_check uptime "load average"
run_check top "PID" "-b" "-n" "1"

# Archive / compression
printf '\n%s── Archive / compression ──%s\n' "$BLUE" "$RESET"
run gzip 0 "-k" "$TMPDIR/hello.txt"
run_check zcat "hello world" "$TMPDIR/hello.txt.gz"
rm -f "$TMPDIR/hello.txt" # Remove original so gunzip can decompress
run gunzip 0 "$TMPDIR/hello.txt.gz"
echo "test data" > "$TMPDIR/tar-file.txt"
run tar 0 "cf" "$TMPDIR/test.tar" "-C" "$TMPDIR" "tar-file.txt"
run_check tar "tar-file.txt" "tf" "$TMPDIR/test.tar"
echo "bzip test data" > "$TMPDIR/bz-file.txt"
run bzip2 0 "-k" "$TMPDIR/bz-file.txt"
run_check bzcat "bzip test data" "$TMPDIR/bz-file.txt.bz2"
rm -f "$TMPDIR/bz-file.txt"
run bunzip2 0 "$TMPDIR/bz-file.txt.bz2"

# Additional utilities
printf '\n%s── Additional utilities ──%s\n' "$BLUE" "$RESET"
run_pipe bc "6" $'2*3\n'
run cmp 0 "$TMPDIR/hello.txt" "$TMPDIR/hello.txt"
echo "different" > "$TMPDIR/diff-other.txt"
run diff 1 "$TMPDIR/hello.txt" "$TMPDIR/diff-other.txt"
run_check strings "hello" "$TMPDIR/hello.txt"
run_check find "hello.txt" "$TMPDIR" "-name" "hello.txt"

# Networking
printf '\n%s── Networking ──%s\n' "$BLUE" "$RESET"
run_check nslookup "Address" "example.com"
run_check wget "Example" "-q" "-O" "-" "http://example.com/"
run_skip ping "needs raw socket / setuid"
run_nc_http_check
run_skip telnet "needs interactive terminal"

# Shell
printf '\n%s── Shell ──%s\n' "$BLUE" "$RESET"
run_pipe ash "hello" "" "-c" "echo hello"
run_pipe sh "hello" "" "-c" "echo hello"

# Summary
total=$((pass + fail + skip))
if [ "$fail" -eq 0 ] && [ "$skip" -eq 0 ]; then
    printf "  All %d tests passed\n" "$pass"
else
    printf "  Results: %d passed, %d failed, %d skipped (of %d)\n" \
        "$pass" "$fail" "$skip" "$total"
fi

if [ "$fail" -gt 0 ]; then
    exit 1
fi
exit 0
