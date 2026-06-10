#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
if [[ -n "${MUPLAR_ROOTFS_CACHE:-}" ]]; then
    CACHE_DIR="$MUPLAR_ROOTFS_CACHE"
elif [[ -x "$SCRIPT_DIR/../../MacOS/mup" ]]; then
    CACHE_DIR="${MUPLAR_HOME:-${HOME:-}/.muplar}/cache/linux-rootfs"
else
    CACHE_DIR="$BUILD_DIR/linux-rootfs-cache"
fi
if [[ -n "${MUP:-}" ]]; then
    MUP="$MUP"
elif [[ -x "$BUILD_DIR/bin/mup" ]]; then
    MUP="$BUILD_DIR/bin/mup"
elif [[ -x "$SCRIPT_DIR/../../MacOS/mup" ]]; then
    MUP="$SCRIPT_DIR/../../MacOS/mup"
else
    MUP="$BUILD_DIR/bin/mup"
fi
TAR_BIN="${TAR:-$(command -v bsdtar || command -v tar || true)}"
CURL_BIN="${CURL:-$(command -v curl || true)}"
PYTHON3_BIN="${PYTHON3:-$(command -v python3 || true)}"

PREFIX=""
DISTRO=""
ARCH=""
PREFIX_ROOT=""
SYSROOT=""
FROM_TAR=""
DOWNLOAD=false
REPLACE_ROOTFS=false
STRIP_COMPONENTS=0

usage() {
    cat <<EOF
Usage:
  $0 --prefix NAME --distro ubuntu|alpine|debian|fedora|arch|opensuse --arch aarch64|x86_64 \\
     (--download | --from-tar PATH) [--root PATH] [--sysroot PATH] [--replace-rootfs] [--strip-components N]

Examples:
  $0 --prefix ubuntu-arm64 --distro ubuntu --arch aarch64 --download
  $0 --prefix alpine-x64 --distro alpine --arch x86_64 --download
  $0 --prefix debian-arm64 --distro debian --arch aarch64 --from-tar ~/rootfs/debian-arm64.tar.xz

Notes:
  - Built-in downloads are currently wired for Ubuntu Base 24.04 and Alpine minirootfs.
  - Debian/Fedora/Arch/openSUSE can be imported from a local rootfs tarball.
  - Use --root to place the instance outside ~/.muplar/prefixes.
EOF
}

fail() {
    echo "ERROR: $*" >&2
    exit 1
}

need_arg() {
    local flag="$1"
    local value="${2:-}"
    if [[ -z "$value" ]]; then
        fail "$flag requires a value"
    fi
}

lower() {
    printf '%s' "$1" | tr '[:upper:]' '[:lower:]'
}

normalize_arch() {
    case "$(lower "$1")" in
        arm64|aarch64)
            echo "aarch64"
            ;;
        x64|x86_64|amd64)
            echo "x86_64"
            ;;
        *)
            fail "unsupported arch '$1' (expected aarch64 or x86_64)"
            ;;
    esac
}

normalize_distro() {
    case "$(lower "$1")" in
        ubuntu|alpine|debian|fedora|arch)
            lower "$1"
            ;;
        opensuse|open-suse|suse)
            echo "opensuse"
            ;;
        *)
            fail "unsupported distro '$1'"
            ;;
    esac
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix)
            need_arg "$1" "${2:-}"
            PREFIX="$2"
            shift 2
            ;;
        --distro)
            need_arg "$1" "${2:-}"
            DISTRO="$(normalize_distro "$2")"
            shift 2
            ;;
        --arch)
            need_arg "$1" "${2:-}"
            ARCH="$(normalize_arch "$2")"
            shift 2
            ;;
        --root)
            need_arg "$1" "${2:-}"
            PREFIX_ROOT="$2"
            shift 2
            ;;
        --sysroot|--runtime)
            need_arg "$1" "${2:-}"
            SYSROOT="$2"
            shift 2
            ;;
        --from-tar)
            need_arg "$1" "${2:-}"
            FROM_TAR="$2"
            shift 2
            ;;
        --download)
            DOWNLOAD=true
            shift
            ;;
        --replace-rootfs)
            REPLACE_ROOTFS=true
            shift
            ;;
        --strip-components)
            need_arg "$1" "${2:-}"
            STRIP_COMPONENTS="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            fail "unknown argument: $1"
            ;;
    esac
done

[[ -n "$PREFIX" ]] || fail "--prefix is required"
[[ -n "$DISTRO" ]] || fail "--distro is required"
[[ -n "$ARCH" ]] || fail "--arch is required"
[[ "$PREFIX" != *"/"* ]] || fail "--prefix expects an instance name; use --root for custom locations"
[[ "$STRIP_COMPONENTS" =~ ^[0-9]+$ ]] || fail "--strip-components must be a non-negative integer"
[[ -x "$MUP" ]] || fail "mup binary not found or not executable: $MUP"
[[ -n "$TAR_BIN" ]] || fail "tar or bsdtar is required"

if [[ "$DOWNLOAD" == true && -n "$FROM_TAR" ]]; then
    fail "choose either --download or --from-tar, not both"
fi
if [[ "$DOWNLOAD" == false && -z "$FROM_TAR" ]]; then
    fail "choose --download or --from-tar PATH"
fi

mkdir -p "$CACHE_DIR"

download_alpine() {
    [[ -n "$CURL_BIN" ]] || fail "curl is required for --download"
    [[ -n "$PYTHON3_BIN" ]] || fail "python3 is required for Alpine metadata parsing"

    local alpine_arch="$ARCH"
    local yaml="$CACHE_DIR/alpine-latest-${alpine_arch}.yaml"
    local release_index="https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/${alpine_arch}/latest-releases.yaml"

    echo "[linux-rootfs] Fetching Alpine release metadata"
    "$CURL_BIN" -fsSL "$release_index" -o "$yaml"

    local metadata
    metadata="$("$PYTHON3_BIN" - "$yaml" <<'PY'
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
        raise SystemExit(0)
raise SystemExit("no alpine-minirootfs release found")
PY
)"

    local branch filename expected_sha256
    IFS=$'\t' read -r branch filename expected_sha256 <<<"$metadata"
    local url="https://dl-cdn.alpinelinux.org/alpine/${branch}/releases/${alpine_arch}/${filename}"
    local tarball="$CACHE_DIR/$filename"

    if [[ ! -f "$tarball" ]]; then
        echo "[linux-rootfs] Downloading $filename"
        "$CURL_BIN" -fL "$url" -o "$tarball.part"
        mv "$tarball.part" "$tarball"
    else
        echo "[linux-rootfs] Using cached $filename"
    fi

    local actual_sha256
    actual_sha256="$(shasum -a 256 "$tarball" | awk '{print $1}')"
    [[ "$actual_sha256" == "$expected_sha256" ]] ||
        fail "Alpine checksum mismatch for $tarball"

    FROM_TAR="$tarball"
}

download_ubuntu() {
    [[ -n "$CURL_BIN" ]] || fail "curl is required for --download"
    [[ -n "$PYTHON3_BIN" ]] || fail "python3 is required for Ubuntu release parsing"

    local ubuntu_arch
    case "$ARCH" in
        aarch64) ubuntu_arch="arm64" ;;
        x86_64) ubuntu_arch="amd64" ;;
        *) fail "unsupported Ubuntu arch: $ARCH" ;;
    esac

    local base_url="https://cdimage.ubuntu.com/ubuntu-base/releases/26.04/release"
    local index="$CACHE_DIR/ubuntu-base-26.04-release.html"
    local sums="$CACHE_DIR/ubuntu-base-26.04-SHA256SUMS"

    echo "[linux-rootfs] Fetching Ubuntu Base release metadata"
    "$CURL_BIN" -fsSL "$base_url/" -o "$index"

    local filename
    filename="$("$PYTHON3_BIN" - "$ubuntu_arch" "$index" <<'PY'
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
    if [[ ! -f "$tarball" ]]; then
        echo "[linux-rootfs] Downloading $filename"
        "$CURL_BIN" -fL "$base_url/$filename" -o "$tarball.part"
        mv "$tarball.part" "$tarball"
    else
        echo "[linux-rootfs] Using cached $filename"
    fi

    if "$CURL_BIN" -fsSL "$base_url/SHA256SUMS" -o "$sums"; then
        local expected_sha256 actual_sha256
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
        if [[ -n "$expected_sha256" ]]; then
            actual_sha256="$(shasum -a 256 "$tarball" | awk '{print $1}')"
            [[ "$actual_sha256" == "$expected_sha256" ]] ||
                fail "Ubuntu checksum mismatch for $tarball"
        fi
    fi

    FROM_TAR="$tarball"
}

if [[ "$DOWNLOAD" == true ]]; then
    case "$DISTRO" in
        alpine)
            download_alpine
            ;;
        ubuntu)
            download_ubuntu
            ;;
        *)
            fail "$DISTRO automatic download is not wired yet; use --from-tar PATH"
            ;;
    esac
fi

[[ -f "$FROM_TAR" ]] || fail "rootfs tarball not found: $FROM_TAR"

prefix_exists_before=false
if "$MUP" prefix info "$PREFIX" >/dev/null 2>&1; then
    prefix_exists_before=true
fi

preexisting_rootfs_contents=false
preexisting_rootfs_path=""
if [[ -n "$PREFIX_ROOT" ]]; then
    preexisting_rootfs_path="$PREFIX_ROOT/rootfs"
    if [[ -d "$PREFIX_ROOT/rootfs" ]] && find "$PREFIX_ROOT/rootfs" -mindepth 1 -maxdepth 1 | read -r _; then
        preexisting_rootfs_contents=true
    fi
elif [[ "$prefix_exists_before" == false ]]; then
    default_home="${MUPLAR_HOME:-${HOME:-}/.muplar}"
    preexisting_rootfs_path="$default_home/prefixes/$PREFIX/rootfs"
    if [[ -n "$default_home" && -d "$default_home/prefixes/$PREFIX/rootfs" ]] &&
       find "$default_home/prefixes/$PREFIX/rootfs" -mindepth 1 -maxdepth 1 | read -r _; then
        preexisting_rootfs_contents=true
    fi
fi

if [[ "$REPLACE_ROOTFS" != true ]]; then
    if [[ "$prefix_exists_before" == true ]]; then
        fail "instance '$PREFIX' already exists; rerun with --replace-rootfs to replace its rootfs"
    fi
    if [[ "$preexisting_rootfs_contents" == true ]]; then
        fail "rootfs already has content at $preexisting_rootfs_path; rerun with --replace-rootfs to replace it"
    fi
fi

prefix_create_args=(prefix create "$PREFIX" --kind linux --arch "$ARCH" --distro "$DISTRO")
if [[ -n "$PREFIX_ROOT" ]]; then
    prefix_create_args+=(--root "$PREFIX_ROOT")
fi
if [[ -n "$SYSROOT" ]]; then
    prefix_create_args+=(--sysroot "$SYSROOT")
fi

echo "[linux-rootfs] Registering instance: $PREFIX ($DISTRO/$ARCH)"
"$MUP" "${prefix_create_args[@]}" >/dev/null

prefix_info="$("$MUP" prefix info "$PREFIX")"
rootfs="$(printf '%s\n' "$prefix_info" | sed -n 's/^Rootfs: //p' | head -n 1)"
[[ -n "$rootfs" ]] || fail "unable to read Rootfs from mup prefix info"

case "$rootfs" in
    /*/rootfs|*/rootfs) ;;
    *) fail "refusing to manage suspicious rootfs path: $rootfs" ;;
esac
[[ "$rootfs" != "/" ]] || fail "refusing to clear /"

if [[ "$prefix_exists_before" == true || "$preexisting_rootfs_contents" == true ]]; then
    if [[ "$REPLACE_ROOTFS" != true ]]; then
        fail "rootfs already exists; rerun with --replace-rootfs to replace $rootfs"
    fi
fi

echo "[linux-rootfs] Preparing rootfs: $rootfs"
mkdir -p "$rootfs"
find "$rootfs" -mindepth 1 -maxdepth 1 -exec rm -rf {} +

echo "[linux-rootfs] Validating archive entries"
while IFS= read -r entry; do
    [[ -z "$entry" ]] && continue
    case "$entry" in
        /*)
            fail "tarball contains absolute path: $entry"
            ;;
    esac
    IFS='/' read -r -a parts <<<"$entry"
    for part in "${parts[@]}"; do
        if [[ "$part" == ".." ]]; then
            fail "tarball contains parent traversal: $entry"
        fi
    done
done < <("$TAR_BIN" -tf "$FROM_TAR")

echo "[linux-rootfs] Extracting $(basename "$FROM_TAR")"
tar_args=(-x -f "$FROM_TAR" -C "$rootfs")
if [[ "$STRIP_COMPONENTS" != "0" ]]; then
    tar_args+=(--strip-components "$STRIP_COMPONENTS")
fi
"$TAR_BIN" "${tar_args[@]}"

echo "[linux-rootfs] Refreshing Muplar scaffold"
"$MUP" "${prefix_create_args[@]}" >/dev/null

cat >"$rootfs/etc/muplar-provisioned" <<EOF
distro=$DISTRO
arch=$ARCH
source=$FROM_TAR
provisioned_at=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
EOF

echo
echo "Provisioned Linux instance:"
"$MUP" prefix info "$PREFIX"
echo
echo "Default terminal package hint:"
if [[ -f "$rootfs/etc/muplar-default-packages" ]]; then
    sed -n 's/^terminal=/  terminal: /p' "$rootfs/etc/muplar-default-packages"
else
    echo "  terminal: install a terminal plus guest Xwayland, such as gnome-terminal xterm xwayland"
fi
