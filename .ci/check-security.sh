#!/usr/bin/env bash

# Security checks for elfuse host source files (src/ only).
# Tests are excluded -- they exercise unsafe patterns deliberately.
#
# 1. Banned functions -- unsafe libc calls with safer alternatives.
# 2. Credential / secret patterns -- catch accidental key leaks.
# 3. Dangerous preprocessor -- detect disabled security features.

set -u -o pipefail

failed=0

# --- Patterns ---
banned='(^|[^[:alnum:]_])(gets|sprintf|vsprintf|strcpy|stpcpy|strcat|atoi|atol|atoll|atof|mktemp|tmpnam|tempnam)[[:space:]]*\('
secrets='(password|secret|api_key|private_key|token)[[:space:]]*=[[:space:]]*"[^"]+'
dangerous_pp='#[[:space:]]*(undef|define)[[:space:]]+((_FORTIFY_SOURCE[[:space:]]+0)|(__SSP__))'
comment_only='^[[:space:]]*(//|/\*|\*|\*/)'

# Only scan elfuse host source, not tests/ or assembly shim.
#
# Each match uses process substitution rather than a shell pipeline:
# under `pipefail`, an early `grep -q` exit closes its stdin, the
# upstream filter receives SIGPIPE, and the pipeline returns non-zero
# even when the pattern matched -- silently dropping real findings.
# Process substitution puts the filter in a separate process whose exit
# status doesn't feed back into the matcher.
while IFS= read -r -d '' f; do
    if grep -qE "$banned" < <(grep -vE "$comment_only" -- "$f"); then
        echo "Banned function in $f:"
        grep -nE "$banned" -- "$f" | grep -vE "$comment_only" || true
        failed=1
    fi
    if grep -iqE "$secrets" < <(grep -vE "$comment_only" -- "$f"); then
        echo "Possible hardcoded secret in $f:"
        grep -inE "$secrets" -- "$f" | grep -vE "$comment_only" || true
        failed=1
    fi
    if grep -qE "$dangerous_pp" < <(grep -vE "$comment_only" -- "$f"); then
        echo "Dangerous preprocessor directive in $f:"
        grep -nE "$dangerous_pp" -- "$f" | grep -vE "$comment_only" || true
        failed=1
    fi
done < <(git ls-files -z -- 'src/*.c' 'src/*.h' 'src/**/*.c' 'src/**/*.h')

if [ $failed -eq 0 ]; then
    echo "Security checks passed."
fi

exit $failed
