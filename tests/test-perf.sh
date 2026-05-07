#!/usr/bin/env bash
# test-perf.sh — Performance comparison: native vs elfuse for grep/wc/cat
#
# Copyright 2026 elfuse contributors
# Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
# SPDX-License-Identifier: Apache-2.0
#
# Measures wall-clock time for common coreutils operations, comparing
# macOS native tools against the same tools running under elfuse. Overhead
# comes from VM startup (~1-3ms) and per-syscall vmexits (~1-5us each).
# Pure computation (regex matching etc.) runs at native speed.
#
# Timing uses bash $EPOCHREALTIME (microsecond precision, no external deps).
#
# Usage: tests/test-perf.sh <elfuse-binary> <tool-bin-dir>
# Example: tests/test-perf.sh build/elfuse /path/to/tool/bin

set -euo pipefail

ELFUSE="${1:?Usage: $0 <elfuse-binary> <tool-bin-dir>}"
TOOL_BIN="${2:?Usage: $0 <elfuse-binary> <tool-bin-dir>}"
SRCDIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_SUBDIR="$SRCDIR/src"
SYSCALL_C="$SRC_SUBDIR/syscall.c"

# Colors
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
RESET='\033[0m'

RUNS=10
PATTERN="syscall"

# Convert $EPOCHREALTIME (seconds.microseconds) to integer microseconds.
# Bash arithmetic can't handle floats, so we split on '.' and combine.
epoch_us()
{
    local t="$EPOCHREALTIME"
    local sec="${t%%.*}"
    local frac="${t##*.}"
    # Pad/truncate frac to 6 digits
    frac="${frac}000000"
    frac="${frac:0:6}"
    echo $((sec * 1000000 + 10#$frac))
}

# Collect $RUNS timing samples for a command, print median and stats.
# Args: label command...
benchmark()
{
    local label="$1"
    shift
    local times=()

    for _ in $(seq 1 $RUNS); do
        local start end us
        start=$(epoch_us)
        "$@" > /dev/null 2>&1 || true
        end=$(epoch_us)
        us=$((end - start))
        # Store as fractional ms string (1 decimal place)
        local ms_int=$((us / 1000))
        local ms_frac=$(((us % 1000) / 100))
        times+=("${ms_int}.${ms_frac}")
    done

    # Sort times numerically and pick median
    local sorted
    sorted=$(printf '%s\n' "${times[@]}" | sort -g)
    local median min max
    median=$(echo "$sorted" | sed -n "$((RUNS / 2 + 1))p")
    min=$(echo "$sorted" | head -1)
    max=$(echo "$sorted" | tail -1)

    printf "  %-22s  median %7s ms  (min %s, max %s)\n" "$label" "$median" "$min" "$max"
}

printf "${BLUE}━━━ Performance: native vs elfuse (%d runs each) ━━━${RESET}\n\n" "$RUNS"

for tool in grep wc cat sort; do
    if [ ! -x "$TOOL_BIN/$tool" ]; then
        printf "missing required guest tool: %s/%s\n" "$TOOL_BIN" "$tool" >&2
        exit 1
    fi
done

# --- Test 1: Recursive grep across elfuse source ---
printf "${YELLOW}▸ grep -r '%s' (recursive, many file opens)${RESET}\n" "$PATTERN"
benchmark "native /usr/bin/grep" /usr/bin/grep -r "$PATTERN" "$SRC_SUBDIR"
benchmark "elfuse guest grep" "$ELFUSE" "$TOOL_BIN/grep" -r "$PATTERN" "$SRC_SUBDIR"
echo

# --- Test 2: Single-file grep (measures startup overhead) ---
printf "${YELLOW}▸ grep -c 'case' syscall.c (single file, startup-dominated)${RESET}\n"
benchmark "native /usr/bin/grep" /usr/bin/grep -c "case" "$SYSCALL_C"
benchmark "elfuse guest grep" "$ELFUSE" "$TOOL_BIN/grep" -c "case" "$SYSCALL_C"
echo

# --- Test 3: wc -l on all source files ---
printf "${YELLOW}▸ wc -l *.c *.h (many small files)${RESET}\n"
benchmark "native /usr/bin/wc" sh -c "/usr/bin/wc -l '$SRC_SUBDIR'/*.c '$SRC_SUBDIR'/*.h"
benchmark "elfuse guest wc" sh -c "'$ELFUSE' '$TOOL_BIN/wc' -l '$SRC_SUBDIR'/*.c '$SRC_SUBDIR'/*.h"
echo

# --- Test 4: I/O throughput — cat large file through wc ---
printf "${YELLOW}▸ cat ~10MiB | wc -l (I/O throughput)${RESET}\n"
TMPFILE=$(mktemp)
trap 'rm -f "$TMPFILE"' EXIT
# Build ~10MiB test file by repeating syscall.c (~100 times)
for _ in $(seq 1 100); do cat "$SYSCALL_C" >> "$TMPFILE"; done
TMPSIZE=$(wc -c < "$TMPFILE" | tr -d ' ')
printf "  ${CYAN}(test file: %s bytes)${RESET}\n" "$TMPSIZE"
benchmark "native cat|wc" sh -c "cat '$TMPFILE' | wc -l"
benchmark "elfuse cat|wc" sh -c "'$ELFUSE' '$TOOL_BIN/cat' '$TMPFILE' | wc -l"
echo

# --- Test 5: sort (CPU + I/O mix) ---
printf "${YELLOW}▸ sort syscall.c (CPU-bound sorting + I/O)${RESET}\n"
benchmark "native /usr/bin/sort" /usr/bin/sort "$SYSCALL_C"
benchmark "elfuse guest sort" "$ELFUSE" "$TOOL_BIN/sort" "$SYSCALL_C"
echo

printf "${BLUE}━━━ Done ━━━${RESET}\n"
printf "${CYAN}Overhead is dominated by: VM startup (~1-3ms), per-syscall vmexit (~1-5us),\n"
printf "and macOS VFS translation. Pure computation runs at native speed.${RESET}\n"
