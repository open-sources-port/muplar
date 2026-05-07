#!/usr/bin/env bash

# Ensure all tracked C/H/S/sh files end with a newline.

set -e -u -o pipefail

ret=0
while IFS= read -rd '' f; do
    # `-b` prints just the encoding (e.g. "us-ascii", "binary", "utf-8")
    # without the filename, so a path containing the word "binary" can't
    # cause a non-binary file to be skipped.
    if [ "$(file -b --mime-encoding -- "$f")" != "binary" ]; then
        if [ -n "$(tail -c1 < "$f")" ]; then
            echo "Warning: No newline at end of file $f"
            ret=1
        fi
    fi
done < <(git ls-files -z -- '*.c' '*.h' '*.S' '*.sh' '*.py' '*.mk' Makefile)

exit $ret
