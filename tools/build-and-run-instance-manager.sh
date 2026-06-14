#!/bin/zsh
set -e

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

SCRIPT_FILE="$ROOT_DIR/tools/build-all-local-only.sh"
echo "running $SCRIPT_FILE..."
"$SCRIPT_FILE"

SCRIPT_FILE="$ROOT_DIR/tools/run-instance-manager.sh"
echo "running $SCRIPT_FILE..."
"$SCRIPT_FILE"
