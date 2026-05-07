#!/usr/bin/env bash
# test-static-bins.sh — Static binary smoke tests for elfuse
#
# Copyright 2026 elfuse contributors
# Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
# SPDX-License-Identifier: Apache-2.0
#
# Tests a variety of standalone static aarch64-linux-musl binaries through elfuse.
# Exercises different runtime profiles: shell interpreters (bash, dash),
# scripting languages (lua, gawk), text tools (grep, sed, find), and
# compute-heavy workloads (fibonacci, mandelbrot).
#
# Usage: tests/test-static-bins.sh <elfuse-binary> <static-bins-dir> [sysroot]
#        where <static-bins-dir> contains: bash, dash, lua, gawk, grep, sed,
#        find, tree, jq, sqlite3, diffutils/diff

set -euo pipefail

ELFUSE="${1:?Usage: $0 <elfuse-binary> <static-bins-dir>}"
BINDIR="${2:?Usage: $0 <elfuse-binary> <static-bins-dir>}"
SYSROOT="${3:-}"

# Source the shared runner so this script reuses the standard reporting
# helpers and pass/fail accounting.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC2034  # Consumed by tests/lib/test-runner.sh.
BIN=""
# shellcheck disable=SC2034  # Consumed by tests/lib/test-runner.sh.
TEST_RUNNER=("$ELFUSE")
# shellcheck disable=SC2034  # Consumed by tests/lib/test-runner.sh.
TEST_LABEL_WIDTH=24
# Per-test wall-clock cap (seconds). Override from CI: TEST_TIMEOUT=30 ...
# shellcheck disable=SC2034  # Consumed by tests/lib/test-runner.sh.
TEST_TIMEOUT="${TEST_TIMEOUT:-10}"
# shellcheck source=tests/lib/test-runner.sh
source "$SCRIPT_DIR/lib/test-runner.sh"

# GNU `timeout` is required to bound each guest invocation. On macOS it
# ships via Homebrew coreutils. Fail loudly upfront instead of letting
# every test report a cryptic "command not found, rc=127".
if ! command -v timeout > /dev/null 2>&1; then
    printf "error: 'timeout' not on PATH (install Homebrew coreutils)\n" >&2
    exit 2
fi

RUNNER=("$ELFUSE")
if [ -n "$SYSROOT" ]; then
    RUNNER+=(--sysroot "$SYSROOT")
fi

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# Resolve the binary under either the flat fixture directory or its bin/
# subdirectory. Use -f rather than -x because guest ELFs are data files on
# macOS and are launched through elfuse instead of directly by the host.
find_bin()
{
    local name="$1"
    if [ -f "$BINDIR/$name" ]; then
        echo "$BINDIR/$name"
    elif [ -f "$BINDIR/bin/$name" ]; then
        echo "$BINDIR/bin/$name"
    else
        echo ""
    fi
}

# Run one guest binary and match its output against a regex. Missing fixture
# binaries count as skips so partial fixture sets still produce a useful run.
srun_check()
{
    local label="$1"
    shift
    local bin="$1"
    shift
    local pattern="$1"
    shift

    if [ -z "$bin" ]; then
        test_report skip "$label" " (binary not found)"
        skip=$((skip + 1))
        return
    fi

    local output rc
    if output=$(timeout "$TEST_TIMEOUT" "${RUNNER[@]}" "$bin" "$@" 2>&1); then rc=0; else rc=$?; fi

    if [ "$rc" -eq 124 ]; then
        test_report fail "$label" " (timeout)"
        fail=$((fail + 1))
    elif echo "$output" | grep -qE "$pattern"; then
        test_report ok "$label"
        pass=$((pass + 1))
    else
        test_report fail "$label" " (pattern '$pattern' not found, rc=$rc)"
        test_excerpt "$output"
        fail=$((fail + 1))
    fi
}

# Variant of srun_check() for tools that consume stdin.
srun_pipe()
{
    local label="$1"
    shift
    local bin="$1"
    shift
    local pattern="$1"
    shift
    local input="$1"
    shift

    if [ -z "$bin" ]; then
        test_report skip "$label" " (binary not found)"
        skip=$((skip + 1))
        return
    fi

    local output rc
    if output=$(printf '%s' "$input" | timeout "$TEST_TIMEOUT" "${RUNNER[@]}" "$bin" "$@" 2>&1); then rc=0; else rc=$?; fi

    if [ "$rc" -eq 124 ]; then
        test_report fail "$label" " (timeout)"
        fail=$((fail + 1))
    elif echo "$output" | grep -qE "$pattern"; then
        test_report ok "$label"
        pass=$((pass + 1))
    else
        test_report fail "$label" " (pattern '$pattern' not found, rc=$rc)"
        test_excerpt "$output"
        fail=$((fail + 1))
    fi
}

# Variant of srun_check() for interpreters that execute a script from disk.
srun_script()
{
    local label="$1"
    shift
    local bin="$1"
    shift
    local pattern="$1"
    shift
    local script_file="$1"
    shift

    if [ -z "$bin" ]; then
        test_report skip "$label" " (binary not found)"
        skip=$((skip + 1))
        return
    fi

    local output rc
    if output=$(timeout "$TEST_TIMEOUT" "${RUNNER[@]}" "$bin" "$@" "$script_file" 2>&1); then rc=0; else rc=$?; fi

    if [ "$rc" -eq 124 ]; then
        test_report fail "$label" " (timeout)"
        fail=$((fail + 1))
    elif echo "$output" | grep -qE "$pattern"; then
        test_report ok "$label"
        pass=$((pass + 1))
    else
        test_report fail "$label" " (pattern '$pattern' not found, rc=$rc)"
        test_excerpt "$output"
        fail=$((fail + 1))
    fi
}

# Resolve all optional fixture binaries up front so each test can skip cheaply.
BASH_BIN=$(find_bin bash)
DASH_BIN=$(find_bin dash)
LUA_BIN=$(find_bin lua)
GAWK_BIN=$(find_bin gawk)
GREP_BIN=$(find_bin grep)
SED_BIN=$(find_bin sed)
FIND_BIN=$(find_bin find)
TREE_BIN=$(find_bin tree)
JQ_BIN=$(find_bin jq)
SQLITE_BIN=$(find_bin sqlite3)
DIFF_BIN=$(find_bin diff)

# Prepare deterministic fixture files shared by multiple tools.
printf 'hello world\n' > "$TMPDIR/hello.txt"
printf 'cherry\napple\nbanana\ndate\nelderberry\n' > "$TMPDIR/fruits.txt"
mkdir -p "$TMPDIR/tree/a/b/c" "$TMPDIR/tree/a/d" "$TMPDIR/tree/e"
printf 'content1\n' > "$TMPDIR/tree/a/b/file1.txt"
printf 'content2\n' > "$TMPDIR/tree/a/b/c/deep.txt"
printf 'content3\n' > "$TMPDIR/tree/a/d/file2.txt"
printf 'content4\n' > "$TMPDIR/tree/e/file3.txt"

printf "\n${BLUE}━━━ Static binary smoke tests ━━━${RESET}\n\n"

# Dash shell
printf "${BLUE}── Dash shell ──${RESET}\n"
srun_check "dash: echo" "$DASH_BIN" "hello" -c "echo hello"
srun_check "dash: arithmetic" "$DASH_BIN" "2\\+3=5" -c 'echo "2+3=$((2+3))"'
srun_check "dash: loop" "$DASH_BIN" "count=10" -c 'i=0; while [ $i -lt 10 ]; do i=$((i+1)); done; echo "count=$i"'
srun_check "dash: conditionals" "$DASH_BIN" "5>3 OK" -c 'if [ 5 -gt 3 ]; then echo "5>3 OK"; fi'
srun_check "dash: case" "$DASH_BIN" "glob match" -c 'case "hello" in hel*) echo "glob match" ;; esac'
srun_check "dash: functions" "$DASH_BIN" "result=15" -c 'sum() { total=0; for n in 1 2 3 4 5; do total=$((total+n)); done; echo "$total"; }; echo "result=$(sum)"'

# Bash shell
printf "\n${BLUE}── Bash shell ──${RESET}\n"

# Keep the Bash coverage in a real script so array syntax, regexes, and
# here-doc quoting are not obscured by nested shell escaping.
cat > "$TMPDIR/bash-test.sh" << 'BASH_SCRIPT'
echo "bash ${BASH_VERSION}"
echo "sum=$((17 * 31))"
arr=(alpha beta gamma delta)
echo "array=${#arr[@]}"
echo "upper=${arr[0]^^}"
declare -A map
map[k]="value"
echo "assoc=${map[k]}"
if [[ "abc123" =~ [0-9]+ ]]; then echo "regex=${BASH_REMATCH[0]}"; fi
printf "printf=%05d\n" 42
echo "done"
BASH_SCRIPT
srun_check "bash: version" "$BASH_BIN" "bash 5" "$TMPDIR/bash-test.sh"
srun_check "bash: arithmetic" "$BASH_BIN" "sum=527" "$TMPDIR/bash-test.sh"
srun_check "bash: arrays" "$BASH_BIN" "array=4" "$TMPDIR/bash-test.sh"
srun_check "bash: uppercase" "$BASH_BIN" "upper=ALPHA" "$TMPDIR/bash-test.sh"
srun_check "bash: assoc arrays" "$BASH_BIN" "assoc=value" "$TMPDIR/bash-test.sh"
srun_check "bash: regex" "$BASH_BIN" "regex=123" "$TMPDIR/bash-test.sh"
srun_check "bash: printf" "$BASH_BIN" "printf=00042" "$TMPDIR/bash-test.sh"

# Exercise Bash's subshell path, which also covers fork handling in elfuse.
srun_check "bash: subshell" "$BASH_BIN" "sub=25" -c 'echo "sub=$(echo $((5*5)))"'

# Lua interpreter
printf "\n${BLUE}── Lua interpreter ──${RESET}\n"
srun_check "lua: hello" "$LUA_BIN" "Hello.*Lua" -e 'print("Hello from " .. _VERSION)'
srun_check "lua: arithmetic" "$LUA_BIN" "3628800" -e 'local r=1; for i=2,10 do r=r*i end; print(r)'

# Use a larger sieve to cover long-running interpreter loops instead of only
# argument parsing and startup.
cat > "$TMPDIR/sieve.lua" << 'LUA_SIEVE'
local function sieve(n)
  local is_prime = {}
  for i = 2, n do is_prime[i] = true end
  for i = 2, math.floor(math.sqrt(n)) do
    if is_prime[i] then
      for j = i*i, n, i do is_prime[j] = false end
    end
  end
  local count = 0
  for i = 2, n do if is_prime[i] then count = count + 1 end end
  return count
end
print("primes=" .. sieve(100000))
LUA_SIEVE
srun_script "lua: sieve(100000)" "$LUA_BIN" "primes=9592" "$TMPDIR/sieve.lua"

# Recursive Fibonacci stresses call frames and interpreter recursion.
srun_check "lua: fib(30)" "$LUA_BIN" "832040" -e 'local function f(n) if n<2 then return n end; return f(n-1)+f(n-2) end; print(f(30))'

# String concatenation covers allocator churn inside the interpreter.
srun_check "lua: strings" "$LUA_BIN" "length=1000" -e 'local s=""; for i=1,1000 do s=s..string.char(65+(i%26)) end; print("length=" .. #s)'

# GNU awk
printf "\n${BLUE}── GNU awk ──${RESET}\n"
srun_pipe "gawk: field split" "$GAWK_BIN" "world" "hello world" '{print $2}'
srun_pipe "gawk: arithmetic" "$GAWK_BIN" "Average: 90" "$(printf 'Alice 90\nBob 85\nCharlie 95')" '{sum+=$2; n++} END{print "Average:", sum/n}'

# The Mandelbrot program exercises numeric loops and repeated string building.
cat > "$TMPDIR/mandel.awk" << 'AWK_MANDEL'
BEGIN {
    rows = 0
    for (y = -1.0; y <= 1.0; y += 0.1) {
        line = ""
        for (x = -2.0; x <= 0.6; x += 0.05) {
            zr = 0; zi = 0; i = 0
            while (i < 50 && zr*zr + zi*zi < 4) {
                tmp = zr*zr - zi*zi + x
                zi = 2*zr*zi + y
                zr = tmp; i++
            }
            if (i >= 50) line = line "#"
            else line = line " "
        }
        rows++
    }
    print "mandelbrot rows=" rows
}
AWK_MANDEL
srun_script "gawk: mandelbrot" "$GAWK_BIN" "mandelbrot rows=21" "$TMPDIR/mandel.awk" -f

# GNU grep
printf "\n${BLUE}── GNU grep ──${RESET}\n"
srun_pipe "grep: basic" "$GREP_BIN" "hello" "hello world" "hello"
srun_pipe "grep: regex" "$GREP_BIN" "brown" "The quick brown fox" -o 'brown\|lazy'
srun_pipe "grep: count" "$GREP_BIN" "^3$" "$(printf 'a\nb\nc')" -c "."
srun_pipe "grep: invert" "$GREP_BIN" "banana" "$(printf 'apple\nbanana\ncherry')" -v "a..le\|c..rry"
srun_check "grep: file" "$GREP_BIN" "cherry" "cherry" "$TMPDIR/fruits.txt"

# GNU sed
printf "\n${BLUE}── GNU sed ──${RESET}\n"
srun_pipe "sed: substitute" "$SED_BIN" "HELLO" "hello" 's/hello/HELLO/'
srun_pipe "sed: delete" "$SED_BIN" "^banana$" "$(printf 'apple\nbanana\ncherry')" '/apple/d'
srun_pipe "sed: numbering" "$SED_BIN" "^2$" "$(printf 'hello\nworld')" '='
srun_pipe "sed: multi-cmd" "$SED_BIN" "earth XXX" "hello world 123" 's/world/earth/; s/[0-9]/X/g'

# GNU find
printf "\n${BLUE}── GNU find ──${RESET}\n"
srun_check "find: by name" "$FIND_BIN" "file1.txt" "$TMPDIR/tree" -name "file1.txt"
srun_check "find: by type" "$FIND_BIN" "/a$" "$TMPDIR/tree" -type d
srun_check "find: all txt" "$FIND_BIN" "deep.txt" "$TMPDIR/tree" -name "*.txt"

# tree
printf "\n${BLUE}── tree ──${RESET}\n"
srun_check "tree: render" "$TREE_BIN" "file1.txt" "$TMPDIR/tree"
srun_check "tree: summary" "$TREE_BIN" "director" "$TMPDIR/tree"

# jq
printf "\n${BLUE}── jq ──${RESET}\n"
srun_pipe "jq: simple" "$JQ_BIN" "^1$" '{"a":1}' '.a'
srun_pipe "jq: array" "$JQ_BIN" "^3$" '[1,2,3]' '.[2]'
srun_pipe "jq: filter" "$JQ_BIN" "Alice" '{"users":[{"name":"Alice","age":30},{"name":"Bob","age":25}]}' '.users[] | select(.age > 28) | .name'
srun_pipe "jq: map" "$JQ_BIN" "\\[2,4,6\\]" '[1,2,3]' -c '[.[] * 2]'
srun_pipe "jq: keys" "$JQ_BIN" '"b"' '{"a":1,"b":2,"c":3}' 'keys[1]'

# SQLite
printf "\n${BLUE}── SQLite ──${RESET}\n"
srun_check "sqlite: version" "$SQLITE_BIN" "^3\\." ":memory:" "SELECT sqlite_version();"
srun_check "sqlite: arithmetic" "$SQLITE_BIN" "^42$" ":memory:" "SELECT 6 * 7;"
srun_check "sqlite: strings" "$SQLITE_BIN" "HELLO" ":memory:" "SELECT upper('hello');"

# Load a small schema script to cover parsing, inserts, and ordered queries.
cat > "$TMPDIR/sqlite-test.sql" << 'SQL'
CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT, score REAL);
INSERT INTO t VALUES (1, 'Alice', 95.5);
INSERT INTO t VALUES (2, 'Bob', 87.3);
INSERT INTO t VALUES (3, 'Charlie', 92.1);
SELECT name || ': ' || score FROM t WHERE score > 90 ORDER BY score DESC;
SQL
srun_check "sqlite: table ops" "$SQLITE_BIN" "Alice.*95.5" ":memory:" ".read $TMPDIR/sqlite-test.sql"

# Recursive CTEs cover more than basic scalar evaluation.
srun_check "sqlite: aggregate" "$SQLITE_BIN" "^55$" ":memory:" "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<10) SELECT sum(x) FROM c;"

# diff
printf "\n${BLUE}── diff ──${RESET}\n"
printf 'different content\n' > "$TMPDIR/other.txt"
# diff returns 1 when files differ but the comparison itself succeeded.
label="diff: different files"
if [ -n "$DIFF_BIN" ]; then
    if output=$(timeout "$TEST_TIMEOUT" "${RUNNER[@]}" "$DIFF_BIN" "$TMPDIR/hello.txt" "$TMPDIR/other.txt" 2>&1); then
        rc=0
    else
        rc=$?
    fi
    if [ "$rc" -eq 124 ]; then
        test_report fail "$label" " (timeout)"
        fail=$((fail + 1))
    elif [ "$rc" = "1" ] && echo "$output" | grep -qE "^[<>]"; then
        test_report ok "$label"
        pass=$((pass + 1))
    else
        test_report fail "$label" " (rc=$rc)"
        test_excerpt "$output"
        fail=$((fail + 1))
    fi
else
    test_report skip "$label" " (binary not found)"
    skip=$((skip + 1))
fi

# diff: identical files (exit code 0, no content diff output)
label="diff: identical"
if [ -n "$DIFF_BIN" ]; then
    if output=$(timeout "$TEST_TIMEOUT" "${RUNNER[@]}" "$DIFF_BIN" "$TMPDIR/hello.txt" "$TMPDIR/hello.txt" 2>&1); then
        test_report ok "$label"
        pass=$((pass + 1))
    else
        rc=$?
        if [ "$rc" -eq 124 ]; then
            test_report fail "$label" " (timeout)"
        else
            test_report fail "$label" " (rc=$rc, expected 0)"
        fi
        fail=$((fail + 1))
    fi
else
    test_report skip "$label" " (binary not found)"
    skip=$((skip + 1))
fi

# Summary
total=$((pass + fail + skip))
printf "\n${BLUE}━━━ Results: %d passed, %d failed, %d skipped (of %d) ━━━${RESET}\n" \
    "$pass" "$fail" "$skip" "$total"

if [ "$fail" -gt 0 ]; then
    exit 1
fi
exit 0
