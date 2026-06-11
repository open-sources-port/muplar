#!/bin/bash
set -euo pipefail

# ---------------------------------------------------------------------------
# tools/setup-linux-bootstrap.sh
#
# Downloads Ubuntu Base and Alpine minirootfs bootstrap tarballs for
# aarch64 and x86_64. These provide real native coreutils as the initial
# userland for Linux instances — replacing the slow BusyBox multicall binary.
#
# After downloading, the tarballs are stored at:
#   third_party/linux-bootstrap/linux-bootstrap-ubuntu-aarch64.tar.gz
#   third_party/linux-bootstrap/linux-bootstrap-ubuntu-x86_64.tar.gz
#   third_party/linux-bootstrap/linux-bootstrap-alpine-aarch64.tar.gz
#   third_party/linux-bootstrap/linux-bootstrap-alpine-x86_64.tar.gz
#
# Usage:
#   tools/setup-linux-bootstrap.sh                # download both distros and archs
#   tools/setup-linux-bootstrap.sh --arch aarch64  # only aarch64
#   tools/setup-linux-bootstrap.sh --force         # re-download even if present
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CACHE_DIR="$ROOT_DIR/.cache"
OUT_DIR="$ROOT_DIR/third_party/linux-bootstrap"

FORCE=false
ARCH=""
UBUNTU_RELEASE="26.04"
BASE_URL="https://cdimage.ubuntu.com/ubuntu-base/releases/${UBUNTU_RELEASE}/release"

usage() {
    cat <<EOF
Usage: $0 [--arch aarch64|x86_64] [--force]

Downloads Ubuntu Base ${UBUNTU_RELEASE} and Alpine minirootfs rootfs tarballs
and stores them in third_party/linux-bootstrap/ for use as Linux instance
bootstrap images.

Run once before building Muplar. Safe to re-run.
EOF
}

fail() {
    echo "ERROR: $*" >&2
    exit 1
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --arch)
            [ "$#" -ge 2 ] || fail "--arch requires aarch64 or x86_64"
            ARCH="$2"; shift 2 ;;
        --force)
            FORCE=true; shift ;;
        -h|--help)
            usage; exit 0 ;;
        *)
            echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

command -v curl >/dev/null 2>&1 || fail "curl is required"
command -v python3 >/dev/null 2>&1 || fail "python3 is required"

mkdir -p "$CACHE_DIR" "$OUT_DIR"

# ---------------------------------------------------------------------------
# Download a single Ubuntu Base rootfs tarball
# ---------------------------------------------------------------------------
download_ubuntu_base() {
    local guest_arch="$1"
    local ubuntu_arch

    case "$guest_arch" in
        aarch64) ubuntu_arch="arm64" ;;
        x86_64)  ubuntu_arch="amd64" ;;
        *)       fail "unsupported arch: $guest_arch" ;;
    esac

    local output="$OUT_DIR/linux-bootstrap-ubuntu-${guest_arch}.tar.gz"

    if [ "$FORCE" = false ] && [ -f "$output" ]; then
        echo "[linux-bootstrap] Already installed: $output"
        echo "[linux-bootstrap] Use --force to re-download."
        return 0
    fi

    local index="$CACHE_DIR/ubuntu-base-${UBUNTU_RELEASE}-release.html"
    local sums="$CACHE_DIR/ubuntu-base-${UBUNTU_RELEASE}-SHA256SUMS"

    echo "[linux-bootstrap] Fetching Ubuntu Base ${UBUNTU_RELEASE} release metadata (${guest_arch})..."
    curl -fsSL "$BASE_URL/" -o "$index"

    local filename
    filename="$(python3 - "$ubuntu_arch" "$index" <<'PY'
import re
import sys

arch = sys.argv[1]
path = sys.argv[2]
text = open(path, encoding="utf-8").read()
pattern = re.compile(rf"ubuntu-base-26\.04(?:\.(\d+))?-base-{re.escape(arch)}\.tar\.gz")
matches = [(int(match.group(1) or 0), match.group(0)) for match in pattern.finditer(text)]
if not matches:
    raise SystemExit(f"no Ubuntu Base 26.04 tarball found for {arch}")
print(max(matches)[1])
PY
)"

    local tarball="$CACHE_DIR/$filename"
    if [ ! -f "$tarball" ] || [ "$FORCE" = true ]; then
        echo "[linux-bootstrap] Downloading $filename..."
        curl -fL --progress-bar "$BASE_URL/$filename" -o "$tarball.part"
        mv "$tarball.part" "$tarball"
    else
        echo "[linux-bootstrap] Using cached: $tarball"
    fi

    # Verify checksum
    if curl -fsSL "$BASE_URL/SHA256SUMS" -o "$sums" 2>/dev/null; then
        local expected_sha256
        expected_sha256="$(
            awk -v f="$filename" '
                {
                    name = $2
                    sub(/^\*/, "", name)
                    sub(/^\.\//, "", name)
                    if (name == f) {
                        print $1
                        exit
                    }
                }
            ' "$sums"
        )"
        if [ -n "$expected_sha256" ]; then
            local actual_sha256
            actual_sha256="$(shasum -a 256 "$tarball" | awk '{print $1}')"
            if [ "$actual_sha256" != "$expected_sha256" ]; then
                rm -f "$tarball"
                fail "SHA256 checksum mismatch for $filename"
            fi
            echo "[linux-bootstrap] ✓ Checksum verified"
        fi
    fi

    cp -f "$tarball" "$output"
    echo "[linux-bootstrap] ✅ Installed: $output ($(du -h "$output" | awk '{print $1}'))"
}

# ---------------------------------------------------------------------------
# Download a single Alpine minirootfs tarball
# ---------------------------------------------------------------------------
download_alpine_base() {
    local guest_arch="$1"
    local output="$OUT_DIR/linux-bootstrap-alpine-${guest_arch}.tar.gz"

    if [ "$FORCE" = false ] && [ -f "$output" ]; then
        echo "[linux-bootstrap] Already installed: $output"
        echo "[linux-bootstrap] Use --force to re-download."
        return 0
    fi

    local yaml="$CACHE_DIR/alpine-latest-${guest_arch}.yaml"
    local release_index="https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/${guest_arch}/latest-releases.yaml"

    echo "[linux-bootstrap] Fetching Alpine release metadata (${guest_arch})..."
    curl -fsSL "$release_index" -o "$yaml"

    local metadata
    metadata="$(python3 - "$yaml" <<'PY'
import sys

path = sys.argv[1]
text = open(path, encoding="utf-8").read()

items = []
current = {}
for raw_line in text.splitlines():
    line = raw_line.rstrip()
    if line == "---":
        continue
    if line == "-":
        if current:
            items.append(current)
        current = {}
        continue
    if ":" not in line or line.startswith("    "):
        continue
    key, value = line.split(":", 1)
    current[key.strip()] = value.strip().strip('"')
if current:
    items.append(current)

for fields in items:
    branch = fields.get("branch", "")
    filename = fields.get("file", "")
    sha256 = fields.get("sha256", "")
    if fields.get("flavor") != "alpine-minirootfs":
        continue
    if not filename.startswith("alpine-minirootfs-"):
        continue
    if branch and filename and sha256:
        print(f"{branch}\t{filename}\t{sha256}")
        sys.exit(0)
sys.exit("no alpine-minirootfs release found")
PY
)"

    local branch filename expected_sha256
    IFS=$'\t' read -r branch filename expected_sha256 <<<"$metadata"
    local url="https://dl-cdn.alpinelinux.org/alpine/${branch}/releases/${guest_arch}/${filename}"
    local tarball="$CACHE_DIR/$filename"

    if [ ! -f "$tarball" ] || [ "$FORCE" = true ]; then
        echo "[linux-bootstrap] Downloading $filename..."
        curl -fL --progress-bar "$url" -o "$tarball.part"
        mv "$tarball.part" "$tarball"
    else
        echo "[linux-bootstrap] Using cached: $tarball"
    fi

    local actual_sha256
    actual_sha256="$(shasum -a 256 "$tarball" | awk '{print $1}')"
    if [ "$actual_sha256" != "$expected_sha256" ]; then
        rm -f "$tarball"
        fail "SHA256 checksum mismatch for Alpine minirootfs"
    fi
    echo "[linux-bootstrap] ✓ Checksum verified"

    cp -f "$tarball" "$output"
    echo "[linux-bootstrap] ✅ Installed: $output ($(du -h "$output" | awk '{print $1}'))"
}

# ---------------------------------------------------------------------------
# Download both architectures (or just the one specified)
# ---------------------------------------------------------------------------
if [ -n "$ARCH" ]; then
    download_ubuntu_base "$ARCH"
    echo
    download_alpine_base "$ARCH"
else
    download_ubuntu_base "aarch64"
    echo
    download_alpine_base "aarch64"
    echo
    download_ubuntu_base "x86_64"
    echo
    download_alpine_base "x86_64"
fi

echo
echo "[linux-bootstrap] Done. Now run: cd build && cmake .. && ninja"
