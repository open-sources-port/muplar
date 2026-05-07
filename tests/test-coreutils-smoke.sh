#!/usr/bin/env bash
# test-coreutils-smoke.sh — Self-contained coreutils smoke tests for elfuse.
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

ELFUSE="${1:?Usage: $0 <elfuse-binary> <coreutils-bin-dir> [sysroot]}"
BIN="${2:?Usage: $0 <elfuse-binary> <coreutils-bin-dir> [sysroot]}"
SYSROOT="${3:-}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_RUNNER=("$ELFUSE")
if [ -n "$SYSROOT" ]; then
    TEST_RUNNER+=(--sysroot "$SYSROOT")
fi
TEST_LABEL_WIDTH=14
TEST_TIMEOUT=10
source "$SCRIPT_DIR/lib/test-runner.sh"
source "$SCRIPT_DIR/lib/coreutils-common.sh"

TMPDIR=$(coreutils_make_tmpdir)
trap 'rm -rf "$TMPDIR"' EXIT

coreutils_populate_fixtures "$TMPDIR"
coreutils_print_suite_header "Dynamic GNU coreutils smoke suite (--sysroot)"

coreutils_print_section "Output / text utilities"
run_check echo "hello" "hello"
run_check cat "hello world" "$TMPDIR/hello.txt"
run_check head "line1" "$TMPDIR/lines.txt"
run_check tail "line5" "$TMPDIR/lines.txt"
run_check wc "5" "-l" "$TMPDIR/lines.txt"
run_check sort "apple" "$TMPDIR/unsorted.txt"
run_pipe tr "HELLO" "hello" "a-z" "A-Z"
run_check seq "5" "1" "5"
run_check expr "3" "1" "+" "2"
run_check factor "2 2 3" "12"
run_check base64 "aGVsbG8" "$TMPDIR/hello.txt"
run_check md5sum "hello.txt" "$TMPDIR/hello.txt"
run_check sha256sum "hello.txt" "$TMPDIR/hello.txt"

coreutils_print_section "File operations"
run cp 0 "$TMPDIR/hello.txt" "$TMPDIR/hello-cp-$$"
run touch 0 "$TMPDIR/touched-$$"
run_check ls "hello" "$TMPDIR"
run_check stat "File:" "$TMPDIR/hello.txt"
run_check basename "hello.txt" "$TMPDIR/hello.txt"
run_check dirname "$TMPDIR" "$TMPDIR/hello.txt"
run_check realpath "hello.txt" "$TMPDIR/hello.txt"
run_check df "Filesystem" "$TMPDIR"
run_check du "[0-9]" "-s" "$TMPDIR"

coreutils_print_section "System info"
run_check uname "Linux" "-s"
run_check date "202" "+%Y"
run_check id "uid="
run_check printenv "/" "PATH"
run_check nproc "[0-9]"

coreutils_print_section "Process utilities"
run true 0
run false 1
run sleep 0 "0"
run env 0 "$BIN/true"
run nice 0 "$BIN/true"
run nohup 0 "$BIN/true"
run_timeout 10 timeout 0 "5" "$BIN/true"

coreutils_print_section "Encoding / hashing"
if [ -e "$BIN/base32" ]; then
    run_check base32 "NBSWY" "$TMPDIR/hello.txt"
fi
run_check sha1sum "hello.txt" "$TMPDIR/hello.txt"
run_check sha512sum "hello.txt" "$TMPDIR/hello.txt"
if [ -e "$BIN/b2sum" ]; then
    run_check b2sum "hello.txt" "$TMPDIR/hello.txt"
fi
run_check cksum "hello.txt" "$TMPDIR/hello.txt"
if [ -e "$BIN/numfmt" ]; then
    run_check numfmt "1\\.0[kK]" "--to=si" "1000"
fi

coreutils_print_summary "Dynamic results"

if [ "$fail" -gt 0 ]; then
    exit 1
fi
exit 0
