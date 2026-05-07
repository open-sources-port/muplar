#!/usr/bin/env bash
# Shared helpers for coreutils shell suites
#
# Copyright 2026 elfuse contributors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

coreutils_make_tmpdir()
{
    mktemp -d "${TMPDIR:-/tmp}/elfuse-coreutils.XXXXXX"
}

coreutils_populate_fixtures()
{
    local tmpdir="${1:?missing tmpdir}"

    printf 'hello world\n' > "$tmpdir/hello.txt"
    printf 'cherry\napple\nbanana\n' > "$tmpdir/unsorted.txt"
    printf 'aaa\nbbb\naaa\nccc\nbbb\n' > "$tmpdir/dups.txt"
    printf 'one\ttwo\tthree\n' > "$tmpdir/tabs.txt"
    printf 'line1\nline2\nline3\nline4\nline5\n' > "$tmpdir/lines.txt"
    printf 'a:b:c\nd:e:f\n' > "$tmpdir/delim.txt"

    mkdir -p "$tmpdir/testdir/sub"
    printf 'file1\n' > "$tmpdir/testdir/file1.txt"
    printf 'file2\n' > "$tmpdir/testdir/sub/file2.txt"
    ln -s "$tmpdir/hello.txt" "$tmpdir/symlink.txt"
}

coreutils_print_suite_header()
{
    local title="${1:?missing title}"
    printf '\n%s%s%s\n\n' "$BLUE" "$title" "$RESET"
}

coreutils_print_section()
{
    local title="${1:?missing title}"
    printf '\n%s%s%s\n' "$BLUE" "$title" "$RESET"
}

coreutils_print_summary()
{
    local label="${1:?missing label}"
    # shellcheck disable=SC2154  # Counters are provided by the shared runner.
    local total=$((pass + fail + expected_fail + skip))

    printf '\n%s%s: %d passed, %d failed, %d xfail, %d skipped (of %d)%s\n' \
        "$BLUE" "$label" "$pass" "$fail" "$expected_fail" "$skip" "$total" \
        "$RESET"
}
