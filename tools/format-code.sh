#!/bin/bash
# Format C/C++ source files in the repository.
#
# Copyright 2026 muplar contributors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

echo "Formatting C/C++ files..."
find cli platform -type f \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" -o -name "*.m" -o -name "*.mm" \) -exec clang-format -i {} +

echo "Formatting completed."
