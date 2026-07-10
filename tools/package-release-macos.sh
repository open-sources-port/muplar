#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
VERSION="${1:-${GITHUB_REF_NAME:-dev}}"
APP_NAME="${MUPLAR_APP_NAME:-Muplar Instance Manager}"
APP_SRC="${MUPLAR_APP_PATH:-$BUILD_DIR/bin/$APP_NAME.app}"
APP_EXECUTABLE="${MUPLAR_APP_EXECUTABLE:-}"
ARCH="${MUPLAR_RELEASE_ARCH:-$(uname -m)}"

normalize_tree_permissions() {
    local path="$1"

    [ -e "$path" ] || return 0

    # macOS-specific cleanup. Ignore errors on Linux or unsupported files.
    chmod -RN "$path" 2>/dev/null || true
    chflags -R nouchg "$path" 2>/dev/null || true
    xattr -cr "$path" 2>/dev/null || true

    # Make the tree readable/traversable by the signing step.
    chmod -R u+rwX,go+rX "$path"

    # Keep directories traversable and files readable.
    find "$path" -type d -exec chmod 755 {} +
    find "$path" -type f -exec chmod u+rw,go+r {} +
}

case "$ARCH" in
  arm64|aarch64) ARCH_NAME="arm64" ;;
  x86_64) ARCH_NAME="x86_64" ;;
  *) ARCH_NAME="$ARCH" ;;
esac

DIST_DIR="$ROOT_DIR/dist"
STAGE="$DIST_DIR/stage-$VERSION-macos-$ARCH_NAME"
APP_STAGE="$STAGE/$APP_NAME.app"
ZIP_PATH="$DIST_DIR/Muplar-$VERSION-macos-$ARCH_NAME.zip"
SHA_PATH="$ZIP_PATH.sha256"

if [ ! -d "$APP_SRC" ]; then
  echo "ERROR: missing app bundle: $APP_SRC" >&2
  echo "Build first, for example:" >&2
  echo "  cmake -S . -B build -G Ninja" >&2
  echo "  cmake --build build" >&2
  exit 1
fi

if [ ! -f "$APP_SRC/Contents/Info.plist" ]; then
  echo "ERROR: not a valid macOS app bundle: $APP_SRC" >&2
  exit 1
fi

if [ -z "$APP_EXECUTABLE" ] && [ -x /usr/libexec/PlistBuddy ]; then
  APP_EXECUTABLE="$(/usr/libexec/PlistBuddy -c "Print :CFBundleExecutable" "$APP_SRC/Contents/Info.plist" 2>/dev/null || true)"
fi
APP_EXECUTABLE="${APP_EXECUTABLE:-$APP_NAME}"

rm -rf "$STAGE" "$ZIP_PATH" "$SHA_PATH"
mkdir -p "$STAGE"

# Copy the built .app as the release payload. Use ditto so macOS bundle
# metadata, symlinks, and resource forks are preserved correctly.
ditto "$APP_SRC" "$APP_STAGE"

FRAMEWORKS_DIR="$APP_STAGE/Contents/Frameworks"
mkdir -p "$FRAMEWORKS_DIR"

# Basic release sanity checks based on the current Muplar bundle layout.
required_paths=(
  "$APP_STAGE/Contents/MacOS/$APP_EXECUTABLE"
  "$APP_STAGE/Contents/Frameworks/angle"
  "$APP_STAGE/Contents/Frameworks/wine"
  "$APP_STAGE/Contents/Frameworks/aarch64"
  "$APP_STAGE/Contents/Frameworks/x86_64"
)

for path in "${required_paths[@]}"; do
  if [ ! -e "$path" ]; then
    echo "WARN: expected bundle path missing: $path" >&2
  fi
done

# Remove local quarantine/resource attributes before signing and zipping.
xattr -cr "$APP_STAGE" 2>/dev/null || true

# Any post-build copy into Contents/Frameworks invalidates the old signature.
# Re-sign nested Mach-O files first, then sign the .app with the hypervisor
# entitlement. For public distribution, set CODESIGN_IDENTITY to a Developer ID
# identity and notarize later; default is ad-hoc for dev releases.
CODESIGN_IDENTITY="${CODESIGN_IDENTITY:--}"
ENTITLEMENTS="${CODESIGN_ENTITLEMENTS:-$ROOT_DIR/mup.entitlements}"

if command -v codesign >/dev/null 2>&1; then
  while IFS= read -r -d '' file_path; do
    if file "$file_path" | grep -Eq 'Mach-O'; then
      codesign --force --sign "$CODESIGN_IDENTITY" --timestamp=none "$file_path" >/dev/null 2>&1 || true
    fi
  done < <(find "$APP_STAGE" -type f -print0)

  if [ -f "$ENTITLEMENTS" ]; then
    codesign --force --deep --sign "$CODESIGN_IDENTITY" \
      --timestamp=none --entitlements "$ENTITLEMENTS" "$APP_STAGE"
  else
    codesign --force --deep --sign "$CODESIGN_IDENTITY" \
      --timestamp=none "$APP_STAGE"
  fi

  codesign --verify --deep --strict "$APP_STAGE"
fi

mkdir -p "$DIST_DIR"
(
  cd "$STAGE"
  ditto -c -k --sequesterRsrc --keepParent "$APP_NAME.app" "$ZIP_PATH"
)
shasum -a 256 "$ZIP_PATH" > "$SHA_PATH"

printf '\nCreated release package:\n  %s\n  %s\n' "$ZIP_PATH" "$SHA_PATH"
