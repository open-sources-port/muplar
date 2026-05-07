#!/usr/bin/env bash
# test-dynamic-coreutils.sh — Dynamically-linked GNU coreutils test suite for elfuse
#
# Copyright 2026 elfuse contributors
# Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
# SPDX-License-Identifier: Apache-2.0
#
# shellcheck disable=SC1091,SC2034,SC2059,SC2154
#
# Mirrors test-coreutils.sh but invokes every tool through elfuse --sysroot,
# exercising the dynamic linker (ld-musl-aarch64.so.1) and shared libc.so.
#
# Usage: tests/test-dynamic-coreutils.sh <elfuse-binary> <sysroot-dir> <coreutils-bin-dir>
# Example: tests/test-dynamic-coreutils.sh build/elfuse $GUEST_SYSROOT $GUEST_DYNAMIC_COREUTILS/bin

set -euo pipefail

ELFUSE="${1:?Usage: $0 <elfuse-binary> <sysroot-dir> <coreutils-bin-dir>}"
SYSROOT="${2:?Usage: $0 <elfuse-binary> <sysroot-dir> <coreutils-bin-dir>}"
BIN="${3:?Usage: $0 <elfuse-binary> <sysroot-dir> <coreutils-bin-dir>}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_RUNNER=("$ELFUSE" --sysroot "$SYSROOT")
TEST_LABEL_WIDTH=14
TEST_TIMEOUT=10
source "$SCRIPT_DIR/lib/test-runner.sh"
source "$SCRIPT_DIR/lib/coreutils-common.sh"

TMPDIR=$(coreutils_make_tmpdir)
trap 'rm -rf "$TMPDIR"' EXIT

coreutils_populate_fixtures "$TMPDIR"

coreutils_print_suite_header "${SUITE_LABEL:-Dynamic GNU coreutils test suite (--sysroot)}"

# Output / text utilities
coreutils_print_section "Output / text utilities"
run_check cat "hello world" "$TMPDIR/hello.txt"
run_check echo "hello" "hello"
run_check printf "42" "%d" 42
# yes writes infinitely — use timeout to limit; rc=124 (timeout) is expected
run_timeout 2 yes 124
run_check head "line1" "$TMPDIR/lines.txt"
run_check tail "line5" "$TMPDIR/lines.txt"
run_check wc "5" "-l" "$TMPDIR/lines.txt"
run_check sort "^apple" "$TMPDIR/unsorted.txt" # verify apple is first (sorted order)
run_check uniq "aaa" "$TMPDIR/dups.txt"
run_check cut "b" "-d:" "-f2" "$TMPDIR/delim.txt"
run_pipe tr "HELLO" "hello" "a-z" "A-Z"
run_check paste "one" "$TMPDIR/tabs.txt"
run_check expand "one" "$TMPDIR/tabs.txt"
run_check unexpand "one" "$TMPDIR/tabs.txt"
run_check fmt "hello world" "$TMPDIR/hello.txt"
run_check fold "hello" "-w5" "$TMPDIR/hello.txt"
run_check nl "hello" "$TMPDIR/hello.txt"
run_check od "0000000" "-c" "$TMPDIR/hello.txt"
run_check pr "hello" "-l20" "$TMPDIR/hello.txt"
run_check tac "line5" "$TMPDIR/lines.txt"
run_check comm "apple" "$TMPDIR/unsorted.txt" "$TMPDIR/unsorted.txt"
run_check join "a:b:c" "$TMPDIR/delim.txt" "$TMPDIR/delim.txt"
run_check ptx "hello" "$TMPDIR/hello.txt"
run_pipe tsort "a" "a b\nb c\n" # topological sort
run_check shuf "line" "-n1" "$TMPDIR/lines.txt"
run split 0 "-l2" "$TMPDIR/lines.txt" "$TMPDIR/split-"
run csplit 0 "$TMPDIR/lines.txt" 3

# Encoding / hashing
coreutils_print_section "Encoding / hashing"
run_check base32 "NBSWY" "$TMPDIR/hello.txt"
run_check base64 "aGVsbG8gd29ybGQ" "$TMPDIR/hello.txt"
run_check basenc "aGVsbG8" "--base64" "$TMPDIR/hello.txt"
run_check md5sum "6f5902ac237024bdd0c176cb93063dc4" "$TMPDIR/hello.txt"
run_check sha1sum "22596363b3de40b06f981fb85d82312e8c0ed511" "$TMPDIR/hello.txt"
run_check sha224sum "95041dd60ab08c0bf5636d50be85fe9790300f39eb84602858a9b430" "$TMPDIR/hello.txt"
run_check sha256sum "a948904f2f0f479b8f8197694b30184b0d2ed1c1cd2a1ec0fb85d299a192a447" "$TMPDIR/hello.txt"
run_check sha384sum "6b3b69ff0a404f28d75e98a066d3fc64fffd9940870cc68bece28545b9a75086" "$TMPDIR/hello.txt"
run_check sha512sum "db3974a97f2407b7cae1ae637c0030687a11913274d578492558e39c16c017de" "$TMPDIR/hello.txt"
run_check b2sum "hello.txt" "$TMPDIR/hello.txt"
run_check cksum "hello.txt" "$TMPDIR/hello.txt"
run_check sum "[0-9]" "$TMPDIR/hello.txt"

# File operations
coreutils_print_section "File operations"
run cp 0 "$TMPDIR/hello.txt" "$TMPDIR/hello-copy.txt"
run mv 0 "$TMPDIR/hello-copy.txt" "$TMPDIR/hello-moved.txt"
run rm 0 "$TMPDIR/hello-moved.txt"
run ln 0 "-s" "$TMPDIR/hello.txt" "$TMPDIR/newlink.txt"
run link 0 "$TMPDIR/hello.txt" "$TMPDIR/hardlink.txt"
run unlink 0 "$TMPDIR/hardlink.txt"
run mkdir 0 "$TMPDIR/newdir"
run rmdir 0 "$TMPDIR/newdir"
run mkfifo 0 "$TMPDIR/testfifo"
run touch 0 "$TMPDIR/touched.txt"
run truncate 0 "-s0" "$TMPDIR/touched.txt"
run shred 0 "-u" "$TMPDIR/touched.txt"
run install 0 "-m" "644" "$TMPDIR/hello.txt" "$TMPDIR/installed.txt"
run dd 0 "if=$TMPDIR/hello.txt" "of=$TMPDIR/dd-out.txt" "bs=12" "count=1"
run sync 0
run mktemp 0 "-p" "$TMPDIR"

# File info
coreutils_print_section "File info"
run_check ls "hello.txt" "$TMPDIR"
run_check dir "hello.txt" "$TMPDIR"
run_check vdir "hello.txt" "$TMPDIR"
run_check stat "File:" "$TMPDIR/hello.txt"
run_check du "[0-9]" "-s" "$TMPDIR"
run_check df "Filesystem" "$TMPDIR"
run_check dircolors "COLOR" "-b"
run_check readlink "$TMPDIR/hello.txt" "$TMPDIR/symlink.txt"
run_check realpath "hello.txt" "$TMPDIR/hello.txt"

# Path utilities
coreutils_print_section "Path utilities"
run_check basename "hello.txt" "$TMPDIR/hello.txt"
run_check dirname "$TMPDIR" "$TMPDIR/hello.txt"
run_check pathchk "" "$TMPDIR/hello.txt"
run_check pwd "/" # some path

# Math / sequence
coreutils_print_section "Math / sequence"
run_check seq "5" "1" "5"
run_check expr "3" "1" "+" "2"
run_check factor "2 2 3" "12"
run_check numfmt "1.0k" "--to=si" "1000"

# System info
coreutils_print_section "System info"
run_check uname "Linux" "-s"
run_check date "202" "+%Y"
run_check nproc "[0-9]"         # prints CPU count
run_check uptime "load average" # reads /proc/uptime + /proc/loadavg
run_check hostid "[0-9a-f]"     # prints hex host ID
run_check printenv "/" "PATH"
run_check id "uid=" # prints uid/gid info

# Process utilities
coreutils_print_section "Process utilities"
run true 0
run false 1
run sleep 0 "0"
run env 0 "$BIN/true"
run nice 0 "$BIN/true"
run nohup 0 "$BIN/true"
run_check kill "TERM" "-l"

# Permissions / ownership
coreutils_print_section "Permissions / ownership"
run chmod 0 "644" "$TMPDIR/hello.txt"
run chown 1 "root:root" "$TMPDIR/hello.txt" # expected to fail (not root)
run chgrp 0 "root" "$TMPDIR/hello.txt"      # succeeds (fchown stub + /etc/group)
run mknod 1 "$TMPDIR/testnode" "c" "1" "1"  # expected to fail (not root)

# User info (limited without /etc/passwd)
coreutils_print_section "User info"
run_check whoami "user"             # reads /etc/passwd (synthetic)
run logname 1                       # exit 1 = "no login name" (no tty)
run_check groups "user"             # reads /etc/group (synthetic)
run_check pinky "Login" "-l" "user" # reads /etc/passwd (synthetic)
run who 0                           # musl getutxent() is a stub; only exit status is meaningful here
run users 0                         # musl getutxent() is a stub; only exit status is meaningful here

# Terminal
coreutils_print_section "Terminal"
run tty 1  # exit 1 = "not a tty" (correct)
run stty 1 # exit 1 = "not a tty" (correct)

# I/O utilities
coreutils_print_section "I/O utilities"
run_pipe tee "hello world" "hello world\n" "$TMPDIR/tee-out.txt"

# Special / test
coreutils_print_section "Special / test"
run test 0 "-f" "$TMPDIR/hello.txt"
run "[" 0 "-f" "$TMPDIR/hello.txt" "]"

# Expected failures / skips
coreutils_print_section "Expected failures / skips"
run_timeout 10 timeout 0 "5" "$BIN/true"
run_skip stdbuf "requires LD_PRELOAD (N/A for elfuse)"
run chroot 0 "/" "$BIN/true"

coreutils_print_summary "${SUITE_SUMMARY:-Dynamic results}"

if [ "$fail" -gt 0 ]; then
    exit 1
fi
exit 0
