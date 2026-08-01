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
    :
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
AR_BIN="${AR:-$(command -v ar || true)}"

PREFIX=""
DISTRO=""
ARCH=""
PREFIX_ROOT=""
SYSROOT=""
FROM_TAR=""
DOWNLOAD=false
REPLACE_ROOTFS=false
STRIP_COMPONENTS=0
BASE_ONLY=false
PACKAGES_ONLY=false

usage() {
    cat <<EOF
Usage:
  $0 --prefix NAME --distro ubuntu|alpine|debian|fedora|arch|opensuse --arch aarch64|x86_64 \\
     (--download | --from-tar PATH) [--root PATH] [--sysroot PATH] [--replace-rootfs] [--strip-components N]

  $0 --prefix NAME --distro DISTRO --arch ARCH --packages-only

Examples:
  $0 --prefix ubuntu-arm64 --distro ubuntu --arch aarch64 --download
  $0 --prefix alpine-x64 --distro alpine --arch x86_64 --download
  $0 --prefix debian-arm64 --distro debian --arch aarch64 --from-tar ~/rootfs/debian-arm64.tar.xz

Notes:
  - Built-in downloads are currently wired for Ubuntu Base 26.04 and Alpine minirootfs.
  - Downloaded base images are shared across instances by distro and architecture.
  - Debian/Fedora/Arch/openSUSE can be imported from a local rootfs tarball.
  - Use --root to place the instance outside ~/.muplar/prefixes.
EOF
}

fail() {
    echo "ERROR: $*" >&2
    exit 1
}

clear_rootfs_dir() {
    local path="$1"
    mkdir -p "$path"
    find "$path" ! -type l -exec chmod u+rwX {} + 2>/dev/null || true
    find "$path" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
}

is_case_insensitive_dir() {
    local path="$1"
    local probe="$path/.muplar-case-probe-$$"
    mkdir -p "$probe"
    : >"$probe/a"
    : >"$probe/A"
    local count
    count="$(find "$probe" -maxdepth 1 -type f | wc -l | tr -d ' ')"
    rm -rf "$probe"
    [[ "$count" == "1" ]]
}

ensure_resolver_config() {
    local resolv="$1/etc/resolv.conf"
    if [[ -L "$resolv" || ! -s "$resolv" ]]; then
        rm -f "$resolv"
        mkdir -p "$1/etc"
        cat >"$resolv" <<'EOF'
nameserver 1.1.1.1
nameserver 8.8.8.8
EOF
    fi
}

ensure_certificate_symlink_config() {
    local root="$1"
    [[ -n "$PYTHON3_BIN" ]] || return 0

    "$PYTHON3_BIN" - "$root" <<'PY'
import os
import sys

root = os.path.realpath(sys.argv[1])
roots = [
    os.path.join(root, "etc", "pki"),
    os.path.join(root, "etc", "ssl"),
    os.path.join(root, "etc", "ca-certificates"),
]

for base in roots:
    if not os.path.isdir(base):
        continue
    for dirpath, dirnames, filenames in os.walk(base):
        names = dirnames + filenames
        for name in names:
            path = os.path.join(dirpath, name)
            if not os.path.islink(path):
                continue
            target = os.readlink(path)
            if not target.startswith("/"):
                continue
            root_target = os.path.realpath(os.path.join(root, target.lstrip("/")))
            if root_target != root and not root_target.startswith(root + os.sep):
                continue
            if not os.path.exists(root_target):
                continue
            rel_target = os.path.relpath(root_target, os.path.dirname(path))
            if rel_target == target:
                continue
            os.unlink(path)
            os.symlink(rel_target, path)
PY
}

ensure_dpkg_casefold_config() {
    local root="$1"
    if ! is_case_insensitive_dir "$root"; then
        return 0
    fi
    local dir="$root/etc/dpkg/dpkg.cfg.d"
    mkdir -p "$dir"
    cat >"$dir/muplar-casefold-terminfo" <<'EOF'
# Muplar rootfs lives on a case-insensitive host filesystem.
# Uppercase terminfo aliases collide with lowercase entries.
path-exclude=/usr/share/terminfo/[A-Z]/*
path-exclude=/usr/share/terminfo/*/*[A-Z]*
path-exclude=/lib/terminfo/[A-Z]/*
path-exclude=/lib/terminfo/*/*[A-Z]*
EOF
}

ensure_apt_cache_config() {
    local root="$1"
    if [[ "$DISTRO" == "ubuntu" || "$DISTRO" == "debian" ]]; then
        local dir="$root/etc/apt/apt.conf.d"
        mkdir -p "$dir"
        cat >"$dir/99cache-limit" <<'EOF'
APT::Cache-Start "100000000";
EOF
    fi
}

ensure_debconf_pipe_compat() {
    local root="$1"
    [[ "$DISTRO" == "ubuntu" || "$DISTRO" == "debian" ]] || return 0
    local confmodule="$root/usr/share/debconf/confmodule"
    [[ -f "$confmodule" && -n "$PYTHON3_BIN" ]] || return 0

    "$PYTHON3_BIN" - "$confmodule" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text()
old_bodies = (
    "\techo STOP >&3\n",
    "\techo STOP >&3 2>/dev/null || true\n",
)
for old in old_bodies:
    if old in text:
        # elfuse does not provide a persistent debconf frontend pipe. Writing
        # STOP to its closed fd delivers SIGPIPE before the shell can handle a
        # failed redirection, leaving otherwise-configured packages broken.
        path.write_text(text.replace(old, "\t:\n", 1))
        break
PY
}

ensure_policy_rc_d() {
    local root="$1"
    if [[ "$DISTRO" == "ubuntu" || "$DISTRO" == "debian" ]]; then
        local policy_file="$root/usr/sbin/policy-rc.d"
        if [[ ! -f "$policy_file" ]]; then
            echo "[linux-rootfs] Creating policy-rc.d to prevent service autostart"
            mkdir -p "$root/usr/sbin"
            cat >"$policy_file" <<'EOF'
#!/bin/sh
exit 101
EOF
            chmod 755 "$policy_file"
        fi
    fi
}

ensure_coreutils_multicall_wrapper() {
    local root="$1"
    if [[ "$DISTRO" == "ubuntu" || "$DISTRO" == "debian" ]]; then
        local coreutils="$root/usr/bin/coreutils"
        local real_coreutils="$root/usr/bin/coreutils.real"

        # Check if already diverted
        if [[ ! -f "$real_coreutils" ]]; then
            echo "[linux-rootfs] Diverting /usr/bin/coreutils to /usr/bin/coreutils.real"
            # Run dpkg-divert in the guest to register the diversion
            run_guest_root 'dpkg-divert --add --rename --divert /usr/bin/coreutils.real /usr/bin/coreutils'
        fi

        cat >"$coreutils" <<'EOF'
#!/bin/sh
cmd="${0##*/}"
if [ "$cmd" = "coreutils" ]; then
    if [ $# -eq 0 ]; then
        exec /usr/bin/coreutils.real coreutils
    else
        exec /usr/bin/coreutils.real "$@"
    fi
else
    exec /usr/bin/coreutils.real "$cmd" "$@"
fi
EOF
        chmod 755 "$coreutils"
    fi

    # Also wrap the cargo uutils-coreutils directory to bypass the Rosetta argv[0] bug in Ubuntu 26.04+
    local cargo_dir="$root/usr/lib/cargo/bin/coreutils"
    local real_dir="$root/usr/lib/cargo/bin/coreutils.real"
    if [[ -d "$cargo_dir" && ! -d "$real_dir" ]]; then
        echo "[linux-rootfs] Wrapping Cargo uutils-coreutils directory to bypass Rosetta bug"
        mv "$cargo_dir" "$real_dir"
        mkdir -p "$cargo_dir"
        
        cat >"$cargo_dir/wrapper.sh" <<'EOF'
#!/bin/sh
cmd="${0##*/}"
exec /usr/lib/cargo/bin/coreutils.real/"$cmd" "$@"
EOF
        chmod 755 "$cargo_dir/wrapper.sh"
        
        # Create symlinks for all applets pointing to the wrapper
        local applet
        for applet in "$real_dir"/*; do
            [[ -f "$applet" ]] || continue
            local name
            name=$(basename "$applet")
            [[ "$name" != "wrapper.sh" ]] || continue
            ln -sf "wrapper.sh" "$cargo_dir/$name"
        done
    fi
}

run_guest_root() {
    DEBIAN_FRONTEND=noninteractive DEBCONF_NONINTERACTIVE_SEEN=true \
        TZ=Etc/UTC "$MUP" --fakeroot --quiet --prefix "$PREFIX" \
        /bin/sh -c "$1"
}

install_sudo() {
    echo "[linux-rootfs] Using Muplar compatibility sudo"
    return 0
}

install_terminal() {
    echo "[linux-rootfs] Installing terminal emulator"

    local rc=0
    local terminal_pkg="gnome-terminal"

    case "$DISTRO" in
        ubuntu|debian)
            run_guest_root \
                "export DEBIAN_FRONTEND=noninteractive DEBCONF_NONINTERACTIVE_SEEN=true TZ=Etc/UTC; apt-get update && apt-get install -y $terminal_pkg" || rc=$?
            ;;

        alpine)
            terminal_pkg="foot"
            run_guest_root \
                "apk add --no-cache $terminal_pkg" || rc=$?
            ;;

        arch)
            terminal_pkg="foot"
            run_guest_root \
                "pacman -Sy --needed --noconfirm $terminal_pkg" || rc=$?
            ;;

        fedora)
            terminal_pkg="ptyxis"
            run_guest_root \
                "dnf -y install $terminal_pkg" || rc=$?
            ;;

        opensuse)
            terminal_pkg="gnome-terminal"
            run_guest_root \
                "zypper --non-interactive install $terminal_pkg" || rc=$?
            ;;

        *)
            echo "[linux-rootfs] WARNING: unsupported distro for terminal install: $DISTRO" >&2
            return 0
            ;;
    esac

    if [[ "$rc" -ne 0 ]]; then
        echo "[linux-rootfs] WARNING: terminal emulator failed to install" >&2
    fi
    return "$rc"
}

# Rebuild the shared desktop caches that GTK reads at startup.
#
# These are normally produced by dpkg triggers, but triggers do not reliably
# fire under the emulated package run, which leaves the caches missing. GTK
# treats several of them as fatal rather than degrading: with no
# /usr/share/mime/mime.cache, gdk-pixbuf cannot content-sniff an SVG whose
# first bytes are "<?xml" rather than "<svg" -- the loader's own prefix
# patterns do not match it -- so loading any themed icon fails with
# "Unrecognized image file format" and gtkiconhelper.c aborts the process.
# For gnome-terminal that abort happens inside the D-Bus-activated server,
# which the client only ever sees as "Message recipient disconnected from
# message bus without replying".
#
# Best-effort per tool: a distro that lacks one of these should not fail
# provisioning over it.
rebuild_desktop_caches() {
    echo "[linux-rootfs] Rebuilding desktop caches"
    run_guest_root '
        set -e
        if command -v update-mime-database >/dev/null 2>&1; then
            update-mime-database /usr/share/mime || true
        fi
        if command -v glib-compile-schemas >/dev/null 2>&1 &&
           [ -d /usr/share/glib-2.0/schemas ]; then
            glib-compile-schemas /usr/share/glib-2.0/schemas || true
        fi
        if command -v gdk-pixbuf-query-loaders >/dev/null 2>&1; then
            gdk-pixbuf-query-loaders --update-cache || true
        fi
        if command -v update-desktop-database >/dev/null 2>&1 &&
           [ -d /usr/share/applications ]; then
            update-desktop-database /usr/share/applications || true
        fi
        if command -v gtk-update-icon-cache >/dev/null 2>&1; then
            for theme in /usr/share/icons/*/; do
                [ -f "$theme/index.theme" ] || continue
                gtk-update-icon-cache -q -f -t "$theme" || true
            done
        fi
        if command -v fc-cache >/dev/null 2>&1; then
            fc-cache -f || true
        fi
    ' || echo "[linux-rootfs] WARNING: desktop cache rebuild incomplete" >&2
    return 0
}

# GTK hands SVG decoding to glycin, which runs each loader in a bubblewrap
# sandbox. bwrap cannot work here at all: it needs unprivileged user namespaces
# and /proc/sys/kernel/overflowuid, neither of which the guest has. Glycin is
# built to cope with that -- its Auto mode probes bwrap and falls back to
# running unsandboxed -- but the probe spawns bwrap, and that spawn aborts the
# whole process before the fallback is ever reached, taking down anything that
# renders an icon.
#
# Divert bwrap so the probe fails with a plain "not found" instead. Glycin then
# takes its unsandboxed path and carries on. dpkg-divert (rather than rm) keeps
# the decision stable across package upgrades and leaves the binary in place
# under a suffix, so it stays recoverable if the sandbox ever becomes viable.
disable_bwrap_sandbox() {
    case "$DISTRO" in
        ubuntu | debian) ;;
        *) return 0 ;;
    esac

    run_guest_root '
        set -e
        [ -x /usr/bin/bwrap ] || exit 0
        command -v dpkg-divert >/dev/null 2>&1 || exit 0
        if dpkg-divert --list /usr/bin/bwrap | grep -q bwrap; then
            exit 0
        fi
        dpkg-divert --local --rename --divert /usr/bin/bwrap.muplar-disabled \
            --add /usr/bin/bwrap
    ' && echo "[linux-rootfs] Disabled bwrap sandbox (glycin runs unsandboxed)" ||
        echo "[linux-rootfs] WARNING: could not divert bwrap" >&2
    return 0
}

ensure_arch_mirror_config() {
    local root="$1"
    local mirrorlist="$root/etc/pacman.d/mirrorlist"
    [[ -f "$mirrorlist" ]] || return 0

    if grep -q 'Muplar preferred mirrors' "$mirrorlist"; then
        return 0
    fi

    local tmp
    tmp="$(mktemp)"
    {
        cat <<'EOF'
# Muplar preferred mirrors
Server = http://nj.us.mirror.archlinuxarm.org/$arch/$repo
Server = http://fl.us.mirror.archlinuxarm.org/$arch/$repo
Server = http://de.mirror.archlinuxarm.org/$arch/$repo

EOF
        sed '/^Server =/s/^/# /' "$mirrorlist"
    } >"$tmp"
    mv "$tmp" "$mirrorlist"
}

ensure_arch_pacman_keyring() {
    local root="$1"
    local keyring="$root/usr/share/pacman/keyrings/archlinuxarm.gpg"
    local gpgdir="$root/etc/pacman.d/gnupg"
    [[ -f "$keyring" ]] || return 0

    rm -rf "$gpgdir"
    mkdir -p "$gpgdir"

    "$MUP" --fakeroot --quiet --prefix "$PREFIX" \
        /bin/sh -c 'set -e
            gpg --dearmor --yes --output /etc/pacman.d/gnupg/pubring.gpg /usr/share/pacman/keyrings/archlinuxarm.gpg
            : > /etc/pacman.d/gnupg/secring.gpg
            : > /etc/pacman.d/gnupg/trustdb.gpg
            chmod 644 /etc/pacman.d/gnupg/pubring.gpg /etc/pacman.d/gnupg/trustdb.gpg
            chmod 600 /etc/pacman.d/gnupg/secring.gpg
        '
}

ensure_arch_pacman_trust_config() {
    local root="$1"
    local conf="$root/etc/pacman.conf"
    [[ -f "$conf" ]] || return 0

    # gpg-agent cannot complete pacman-key's local-signing flow in Muplar yet.
    # Keep package signatures required, but trust keys already imported into
    # the local pacman keyring.
    sed -i.bak \
        -e 's/^SigLevel[[:space:]]*=.*/SigLevel    = Required DatabaseOptional TrustAll/' \
        -e 's/^DownloadUser[[:space:]]*=/# DownloadUser =/' \
        -e 's|^#XferCommand[[:space:]]*=[[:space:]]*/usr/bin/curl.*|XferCommand = /usr/bin/curl -L -C - -f -o %o %u|' \
        "$conf"
}

ensure_arch_pacman_local_db_config() {
    local root="$1"
    local local_db="$root/var/lib/pacman/local"
    [[ -d "$local_db" ]] || return 0

    # A failed host-side extraction or interrupted pacman transaction can leave
    # empty package database directories behind. libalpm aborts later upgrades
    # when it tries to create the same local package directory.
    find "$local_db" -mindepth 1 -maxdepth 1 -type d ! -name '.*' \
        ! -exec test -f '{}/desc' ';' -print -exec rm -rf '{}' '+'
}

ensure_arch_profile_config() {
    local root="$1"
    local gpm="$root/etc/profile.d/gpm.sh"
    [[ -f "$gpm" ]] || return 0

    # Muplar command launches do not currently expose a Linux ttyname. Arch's
    # gpm profile hook probes tty on every login shell, so silence only that
    # probe while preserving its behavior on real /dev/ttyN consoles.
    # shellcheck disable=SC2016
    sed -i.bak \
        -e 's|case $( /usr/bin/tty ) in|case $( /usr/bin/tty 2>/dev/null ) in|' \
        "$gpm"
}

install_debian_dpkg_deb_wrapper() {
    local root="$1"
    local dpkg_deb="$root/usr/bin/dpkg-deb"
    [[ -x "$dpkg_deb" ]] || return 0

    if [[ ! -x "$root/usr/bin/dpkg-deb.real" ]]; then
        mv "$dpkg_deb" "$root/usr/bin/dpkg-deb.real"
    fi

    cat >"$dpkg_deb" <<'EOF'
#!/bin/sh
real=/usr/bin/dpkg-deb.real

cleanup_files=
tmp_index=0
tmp_path=
cleanup() {
    for path in $cleanup_files; do
        rm -rf "$path"
    done
}
trap cleanup EXIT HUP INT TERM

make_tmp_file() {
    tmp_index=$((tmp_index + 1))
    path="/tmp/dpkg-deb-wrapper.$$.$tmp_index"
    : >"$path" || exit 2
    cleanup_files="$cleanup_files $path"
    tmp_path=$path
}

make_tmp_dir() {
    tmp_index=$((tmp_index + 1))
    path="/tmp/dpkg-deb-wrapper.$$.$tmp_index.dir"
    rm -rf "$path"
    mkdir -p "$path" || exit 2
    cleanup_files="$cleanup_files $path"
    tmp_path=$path
}

member_to_tar_file() {
    deb=$1
    kind=$2
    out=$3
    make_tmp_file
    compressed=$tmp_path

    for member in "$kind.tar.xz" "$kind.tar.gz" "$kind.tar.zst" "$kind.tar"; do
        if ar p "$deb" "$member" >"$compressed" 2>/dev/null && [ -s "$compressed" ]; then
            case "$member" in
                *.tar.xz) xz -dc "$compressed" >"$out" || exit 2 ;;
                *.tar.gz) gzip -dc "$compressed" >"$out" || exit 2 ;;
                *.tar.zst) zstd -dc "$compressed" >"$out" || exit 2 ;;
                *.tar) cat "$compressed" >"$out" || exit 2 ;;
            esac
            return 0
        fi
        : >"$compressed"
    done

    echo "dpkg-deb: error: missing $kind archive in $deb" >&2
    exit 2
}

while [ $# -gt 0 ]; do
    case "$1" in
        --threads-max=*) shift ;;
        --threads-max) shift 2 ;;
        --debug|--verbose|--nocheck|--root-owner-group|--no-uniform-compression|--uniform-compression) shift ;;
        *) break ;;
    esac
done

command=$1
[ $# -gt 0 ] && shift

case "$command" in
    --ctrl-tarfile)
        [ $# -eq 1 ] || exec "$real" "$command" "$@"
        make_tmp_file
        tarfile=$tmp_path
        member_to_tar_file "$1" control "$tarfile"
        cat "$tarfile"
        ;;
    --fsys-tarfile)
        [ $# -eq 1 ] || exec "$real" "$command" "$@"
        make_tmp_file
        tarfile=$tmp_path
        member_to_tar_file "$1" data "$tarfile"
        cat "$tarfile"
        ;;
    --control|-e)
        [ $# -ge 1 ] || exec "$real" "$command" "$@"
        deb=$1
        dir=${2:-DEBIAN}
        mkdir -p "$dir" || exit 2
        make_tmp_file
        tarfile=$tmp_path
        member_to_tar_file "$deb" control "$tarfile"
        tar -xf "$tarfile" -C "$dir"
        ;;
    --extract|-x)
        [ $# -eq 2 ] || exec "$real" "$command" "$@"
        mkdir -p "$2" || exit 2
        make_tmp_file
        tarfile=$tmp_path
        member_to_tar_file "$1" data "$tarfile"
        tar -xf "$tarfile" -C "$2"
        ;;
    --vextract|-X)
        [ $# -eq 2 ] || exec "$real" "$command" "$@"
        mkdir -p "$2" || exit 2
        make_tmp_file
        tarfile=$tmp_path
        member_to_tar_file "$1" data "$tarfile"
        tar -xvf "$tarfile" -C "$2"
        ;;
    --info|-I)
        [ $# -ge 1 ] || exec "$real" "$command" "$@"
        deb=$1
        shift
        make_tmp_dir
        dir=$tmp_path
        make_tmp_file
        tarfile=$tmp_path
        member_to_tar_file "$deb" control "$tarfile"
        tar -xf "$tarfile" -C "$dir" || exit 2
        if [ $# -gt 0 ]; then
            for field in "$@"; do
                [ -f "$dir/$field" ] && cat "$dir/$field"
            done
        else
            echo " new Debian package, version 2.0."
            [ -f "$dir/control" ] && cat "$dir/control"
        fi
        ;;
    *)
        exec "$real" "$command" "$@"
        ;;
esac
EOF
    chmod 755 "$dpkg_deb"
}

host_extract_deb_data() {
    local deb="$1"
    local root="$2"
    local tmp
    tmp="$(mktemp -d)"
    (
        cd "$tmp"
        "$AR_BIN" x "$deb"
        if [[ -f data.tar.xz ]]; then
            "$TAR_BIN" -xpf data.tar.xz -C "$root"
        elif [[ -f data.tar.gz ]]; then
            "$TAR_BIN" -xpf data.tar.gz -C "$root"
        elif [[ -f data.tar.zst ]]; then
            "$TAR_BIN" -xpf data.tar.zst -C "$root"
        elif [[ -f data.tar ]]; then
            "$TAR_BIN" -xpf data.tar -C "$root"
        else
            fail "no data archive in $deb"
        fi
    )
    rm -rf "$tmp"
}

ensure_debian_dpkg_deb_config() {
    local root="$1"
    [[ "$DISTRO" == "debian" ]] || return 0
    [[ -n "$AR_BIN" ]] || fail "ar is required for Debian dpkg-deb bootstrap"

    local binutils_arch_pkg
    case "$ARCH" in
        aarch64) binutils_arch_pkg="binutils-aarch64-linux-gnu" ;;
        x86_64) binutils_arch_pkg="binutils-x86-64-linux-gnu" ;;
        *) fail "unsupported Debian arch for binutils bootstrap: $ARCH" ;;
    esac

    echo "[linux-rootfs] Bootstrapping Debian dpkg-deb compatibility"
    "$MUP" --fakeroot --quiet --prefix "$PREFIX" \
        /bin/sh -c "set -e
            cd /home/muplar
            apt-get update
            apt-get download xz-utils binutils binutils-common libbinutils ${binutils_arch_pkg}
        "

    local debs=()
    while IFS= read -r deb; do
        debs+=("$deb")
        host_extract_deb_data "$deb" "$root"
    done < <(find "$root/home/muplar" -maxdepth 1 -type f \
        \( -name 'xz-utils_*.deb' -o -name 'binutils_*.deb' -o \
           -name 'binutils-common_*.deb' -o -name 'libbinutils_*.deb' -o \
           -name "${binutils_arch_pkg}_*.deb" \) | sort)

    install_debian_dpkg_deb_wrapper "$root"

    if [[ "${#debs[@]}" -gt 0 ]]; then
        local guest_debs=()
        for deb in "${debs[@]}"; do
            guest_debs+=("/home/muplar/$(basename "$deb")")
        done
        DEBIAN_FRONTEND=noninteractive \
            "$MUP" --fakeroot --quiet --prefix "$PREFIX" /usr/bin/dpkg -i "${guest_debs[@]}" || true
        DEBIAN_FRONTEND=noninteractive \
            "$MUP" --fakeroot --quiet --prefix "$PREFIX" /usr/bin/apt-get -f install -y
    fi
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
        --base-only)
            BASE_ONLY=true
            shift
            ;;
        --packages-only)
            PACKAGES_ONLY=true
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

if [[ "$BASE_ONLY" == true && "$PACKAGES_ONLY" == true ]]; then
    fail "choose either --base-only or --packages-only"
fi
if [[ "$PACKAGES_ONLY" != true && "$DOWNLOAD" == true && -n "$FROM_TAR" ]]; then
    fail "choose either --download or --from-tar, not both"
fi
if [[ "$PACKAGES_ONLY" != true && "$DOWNLOAD" == false && -z "$FROM_TAR" ]]; then
    fail "choose --download or --from-tar PATH"
fi

mkdir -p "$CACHE_DIR"

if [[ "$PACKAGES_ONLY" == true ]]; then
    prefix_info="$("$MUP" prefix info "$PREFIX")"
    rootfs="$(printf '%s\n' "$prefix_info" | sed -n 's/^Rootfs: //p' | head -n 1)"
    [[ -n "$rootfs" && -d "$rootfs" ]] || fail "unable to find rootfs for $PREFIX"
    ensure_resolver_config "$rootfs"
    ensure_certificate_symlink_config "$rootfs"
    ensure_debconf_pipe_compat "$rootfs"
    package_rc=0
    install_sudo || package_rc=$?
    install_terminal || package_rc=$?
    disable_bwrap_sandbox
    rebuild_desktop_caches
    if [[ "$package_rc" -eq 0 ]]; then
        rm -f "$rootfs/etc/muplar-default-packages-pending"
        echo "[linux-rootfs] Default package installation finished"
    else
        echo "[linux-rootfs] Default package installation failed" >&2
    fi
    exit "$package_rc"
fi

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

    # Resolve an existing architecture-specific image before contacting the
    # release server. SHA256SUMS is retained beside the archive, so repeated
    # instance creation remains both offline-capable and verified.
    local cached_tarball cached_filename cached_expected cached_actual
    cached_tarball="$(find "$CACHE_DIR" -maxdepth 1 -type f \
        -name "ubuntu-base-26.04*-base-${ubuntu_arch}.tar.gz" \
        ! -name '*.part' | sort -V | tail -n 1)"
    if [[ -n "$cached_tarball" && -s "$cached_tarball" ]]; then
        cached_filename="$(basename "$cached_tarball")"
        cached_expected=""
        if [[ -s "$sums" ]]; then
            cached_expected="$(awk -v f="$cached_filename" '
                {
                    name = $2
                    sub(/^\*/, "", name)
                    sub(/^\.\//, "", name)
                    if (name == f) { print $1; exit }
                }
            ' "$sums")"
        fi
        if [[ -n "$cached_expected" ]]; then
            cached_actual="$(shasum -a 256 "$cached_tarball" | awk '{print $1}')"
            if [[ "$cached_actual" == "$cached_expected" ]]; then
                echo "[linux-rootfs] Using verified cached $cached_filename"
                FROM_TAR="$cached_tarball"
                return
            fi
            echo "[linux-rootfs] Cached checksum failed; downloading a fresh image" >&2
            rm -f "$cached_tarball"
        else
            echo "[linux-rootfs] Using cached $cached_filename"
            FROM_TAR="$cached_tarball"
            return
        fi
    fi

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

download_debian() {
    [[ -n "$CURL_BIN" ]] || fail "curl is required for --download"

    local debian_branch
    case "$ARCH" in
        aarch64) debian_branch="dist-arm64v8" ;;
        x86_64) debian_branch="dist-amd64" ;;
        *) fail "unsupported Debian arch: $ARCH" ;;
    esac

    local url="https://github.com/debuerreotype/docker-debian-artifacts/raw/${debian_branch}/bookworm/oci/blobs/rootfs.tar.gz"
    local tarball="$CACHE_DIR/debian-bookworm-${ARCH}.tar.gz"

    if [[ ! -f "$tarball" ]]; then
        echo "[linux-rootfs] Downloading debian-bookworm-${ARCH}.tar.gz"
        "$CURL_BIN" -fL "$url" -o "$tarball.part"
        mv "$tarball.part" "$tarball"
    else
        echo "[linux-rootfs] Using cached debian-bookworm-${ARCH}.tar.gz"
    fi

    FROM_TAR="$tarball"
}

download_fedora() {
    [[ -n "$CURL_BIN" ]] || fail "curl is required for --download"

    local url="https://archives.fedoraproject.org/pub/archive/fedora/linux/releases/40/Container/${ARCH}/images/Fedora-Container-Base-Generic.${ARCH}-40-1.14.oci.tar.xz"
    local tarball="$CACHE_DIR/fedora-40-${ARCH}.oci.tar.xz"

    if [[ ! -f "$tarball" ]]; then
        echo "[linux-rootfs] Downloading fedora-40-${ARCH}.oci.tar.xz"
        "$CURL_BIN" -fL "$url" -o "$tarball.part"
        mv "$tarball.part" "$tarball"
    else
        echo "[linux-rootfs] Using cached fedora-40-${ARCH}.oci.tar.xz"
    fi

    FROM_TAR="$tarball"
}

download_arch() {
    [[ -n "$CURL_BIN" ]] || fail "curl is required for --download"

    local url
    local ext
    if [[ "$ARCH" == "aarch64" ]]; then
        url="http://archlinuxarm.org/os/ArchLinuxARM-aarch64-latest.tar.gz"
        ext="tar.gz"
    else
        url="https://geo.mirror.pkgbuild.com/iso/latest/archlinux-bootstrap-x86_64.tar.zst"
        ext="tar.zst"
    fi
    local tarball="$CACHE_DIR/archlinux-bootstrap-${ARCH}.${ext}"

    if [[ ! -f "$tarball" ]]; then
        echo "[linux-rootfs] Downloading archlinux-bootstrap-${ARCH}.${ext}"
        "$CURL_BIN" -fL "$url" -o "$tarball.part"
        mv "$tarball.part" "$tarball"
    else
        echo "[linux-rootfs] Using cached archlinux-bootstrap-${ARCH}.${ext}"
    fi

    FROM_TAR="$tarball"
}

download_opensuse() {
    [[ -n "$CURL_BIN" ]] || fail "curl is required for --download"

    local url
    if [[ "$ARCH" == "aarch64" ]]; then
        url="https://download.opensuse.org/ports/aarch64/tumbleweed/appliances/opensuse-tumbleweed-image.aarch64-lxc.tar.xz"
    else
        url="https://download.opensuse.org/tumbleweed/appliances/opensuse-tumbleweed-image.x86_64-lxc.tar.xz"
    fi
    local tarball="$CACHE_DIR/opensuse-tumbleweed-lxc-${ARCH}.tar.xz"

    if [[ ! -f "$tarball" ]]; then
        echo "[linux-rootfs] Downloading opensuse-tumbleweed-lxc-${ARCH}.tar.xz"
        "$CURL_BIN" -fL "$url" -o "$tarball.part"
        mv "$tarball.part" "$tarball"
    else
        echo "[linux-rootfs] Using cached opensuse-tumbleweed-lxc-${ARCH}.tar.xz"
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
        debian)
            download_debian
            ;;
        fedora)
            download_fedora
            ;;
        arch)
            download_arch
            ;;
        opensuse)
            download_opensuse
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
if [[ "$prefix_exists_before" == true ]]; then
    prefix_info_before="$("$MUP" prefix info "$PREFIX")"
    preexisting_rootfs_path="$(printf '%s\n' "$prefix_info_before" | sed -n 's/^Rootfs: //p' | head -n 1)"
    if [[ -n "$preexisting_rootfs_path" && -d "$preexisting_rootfs_path" ]] &&
       find "$preexisting_rootfs_path" -mindepth 1 -maxdepth 1 | read -r _; then
        preexisting_rootfs_contents=true
    fi
elif [[ -n "$PREFIX_ROOT" ]]; then
    preexisting_rootfs_path="$PREFIX_ROOT/rootfs"
    if [[ -d "$PREFIX_ROOT/rootfs" ]] && find "$PREFIX_ROOT/rootfs" -mindepth 1 -maxdepth 1 | read -r _; then
        preexisting_rootfs_contents=true
    fi
else
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

if [[ "$REPLACE_ROOTFS" == true && "$preexisting_rootfs_contents" == true ]]; then
    case "$preexisting_rootfs_path" in
        /*/rootfs|*/rootfs) ;;
        *) fail "refusing to manage suspicious rootfs path: $preexisting_rootfs_path" ;;
    esac
    [[ "$preexisting_rootfs_path" != "/" ]] || fail "refusing to clear /"
    clear_rootfs_dir "$preexisting_rootfs_path"
fi

prefix_create_args=(prefix create "$PREFIX" --kind linux --arch "$ARCH" --distro "$DISTRO")
if [[ -n "$PREFIX_ROOT" ]]; then
    prefix_create_args+=(--root "$PREFIX_ROOT")
fi
if [[ -n "$SYSROOT" ]]; then
    prefix_create_args+=(--sysroot "$SYSROOT")
fi

echo "[linux-rootfs] Registering instance: $PREFIX ($DISTRO/$ARCH)"
MUPLAR_SKIP_LINUX_BOOTSTRAP=1 "$MUP" "${prefix_create_args[@]}" >/dev/null

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
clear_rootfs_dir "$rootfs"

echo "[linux-rootfs] Validating archive entries"
TARBALL_TO_EXTRACT="$FROM_TAR"
OCI_TMP_DIR=""
if [[ "$FROM_TAR" == *.oci.tar.xz ]]; then
    echo "[linux-rootfs] OCI container image detected, extracting rootfs layer..."
    OCI_TMP_DIR="$(mktemp -d)"
    "$TAR_BIN" -xf "$FROM_TAR" -C "$OCI_TMP_DIR"
    # Find the largest file in blobs/sha256/
    rootfs_layer="$(find "$OCI_TMP_DIR" -type f -print0 | xargs -0 ls -S | head -n 1)"
    TARBALL_TO_EXTRACT="$rootfs_layer"
fi

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
done < <("$TAR_BIN" -tf "$TARBALL_TO_EXTRACT")

echo "[linux-rootfs] Extracting $(basename "$FROM_TAR")"
tar_args=(-x -f "$TARBALL_TO_EXTRACT" -C "$rootfs")
if [[ "$DISTRO" == "arch" ]] && is_case_insensitive_dir "$rootfs"; then
    echo "[linux-rootfs] Skipping Arch terminfo aliases on case-insensitive filesystem"
    tar_args=(-x \
              --exclude './usr/share/terminfo/*' \
              --exclude 'usr/share/terminfo/*' \
              -f "$TARBALL_TO_EXTRACT" -C "$rootfs")
fi
if [[ "$STRIP_COMPONENTS" != "0" ]]; then
    tar_args+=(--strip-components "$STRIP_COMPONENTS")
fi
"$TAR_BIN" "${tar_args[@]}"

if [[ -n "$OCI_TMP_DIR" ]]; then
    rm -rf "$OCI_TMP_DIR"
fi

# Some distro appliances ship top-level placeholders such as /home with
# read-only modes. Muplar needs to add /home/muplar during scaffold refresh.
if [[ -d "$rootfs/home" ]]; then
    chmod u+rwx "$rootfs/home"
fi
mkdir -p "$rootfs/tmp" "$rootfs/var/tmp"
chmod 1777 "$rootfs/tmp" "$rootfs/var/tmp"
ensure_resolver_config "$rootfs"
ensure_certificate_symlink_config "$rootfs"
ensure_dpkg_casefold_config "$rootfs"
ensure_apt_cache_config "$rootfs"
ensure_debconf_pipe_compat "$rootfs"
ensure_policy_rc_d "$rootfs"
ensure_coreutils_multicall_wrapper "$rootfs"
if [[ "$DISTRO" == "arch" ]]; then
    ensure_arch_pacman_trust_config "$rootfs"
    ensure_arch_pacman_local_db_config "$rootfs"
    ensure_arch_mirror_config "$rootfs"
    ensure_arch_profile_config "$rootfs"
fi

echo "[linux-rootfs] Refreshing Muplar scaffold"
MUPLAR_SKIP_LINUX_BOOTSTRAP=1 "$MUP" "${prefix_create_args[@]}" >/dev/null
ensure_resolver_config "$rootfs"
ensure_certificate_symlink_config "$rootfs"
ensure_debconf_pipe_compat "$rootfs"
ensure_policy_rc_d "$rootfs"
ensure_coreutils_multicall_wrapper "$rootfs"
if [[ "$DISTRO" == "debian" ]]; then
    ensure_debian_dpkg_deb_config "$rootfs"
fi
if [[ "$DISTRO" == "arch" ]]; then
    ensure_arch_pacman_keyring "$rootfs"
    ensure_arch_pacman_trust_config "$rootfs"
    ensure_arch_pacman_local_db_config "$rootfs"
    ensure_arch_mirror_config "$rootfs"
    ensure_arch_profile_config "$rootfs"
fi

if [[ "$BASE_ONLY" == true ]]; then
    : >"$rootfs/etc/muplar-default-packages-pending"
    echo "[linux-rootfs] Base instance ready; default packages deferred"
else
    install_sudo || true
    install_terminal || true
    disable_bwrap_sandbox
    rebuild_desktop_caches
    rm -f "$rootfs/etc/muplar-default-packages-pending"
fi

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
