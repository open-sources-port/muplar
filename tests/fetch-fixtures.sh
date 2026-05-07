#!/usr/bin/env bash
# fetch-fixtures.sh — Download Alpine packages and assemble a qemu/elfuse
# test fixture tree under externals/test-fixtures/.
#
# Idempotent: re-runs are no-ops once everything is in place.  Re-execute
# with FORCE=1 to rebuild from scratch.
#
# Layout:
#   externals/test-fixtures/
#     cache/                       # downloaded .apk + .tar.gz
#     kernel/vmlinuz-virt          # extracted aarch64 kernel image
#     rootfs/                      # extracted minirootfs + overlays
#     initramfs.cpio.gz            # built from rootfs/
#     keys/{ssh_key,ssh_key.pub}   # generated ssh keypair
#     aarch64-musl/
#       staticbin/bin/             # busybox-static + applet symlinks
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

ALPINE_VERSION="${ALPINE_VERSION:-3.21}"
ALPINE_PATCH="${ALPINE_PATCH:-3.21.0}"
ALPINE_ARCH="${ALPINE_ARCH:-aarch64}"

CDN_BASE="https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_VERSION}"
RELEASES="${CDN_BASE}/releases/${ALPINE_ARCH}"
MAIN_REPO="${CDN_BASE}/main/${ALPINE_ARCH}"
COMMUNITY_REPO="${CDN_BASE}/community/${ALPINE_ARCH}"

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FIXTURES="${REPO_ROOT}/externals/test-fixtures"
CACHE="${FIXTURES}/cache"
ROOTFS="${FIXTURES}/rootfs"
KERNEL_DIR="${FIXTURES}/kernel"
KEYS_DIR="${FIXTURES}/keys"
STATICBIN="${FIXTURES}/aarch64-musl/staticbin/bin"
INITRAMFS="${FIXTURES}/initramfs.cpio.gz"

# Pinned package versions (Alpine 3.21).  When bumping ALPINE_VERSION, refresh
# these by querying the repo's APKINDEX.
declare -A PKGS=(
    ["main:linux-virt"]="6.12.85-r0"
    ["main:busybox-static"]="1.37.0-r14"
    ["main:dropbear"]="2024.86-r0"
    ["main:zlib"]="1.3.2-r0"
    ["main:utmps-libs"]="0.1.2.3-r2"
    ["main:skalibs-libs"]="2.14.3.0-r0"
    ["main:musl"]="1.2.5-r11"
    ["main:musl-utils"]="1.2.5-r11"
    ["main:libcrypto3"]="3.3.7-r0"
    ["main:acl-libs"]="2.3.2-r1"
    ["main:libattr"]="2.5.2-r2"
    ["main:pcre2"]="10.43-r0"
    ["main:coreutils"]="9.5-r2"
    ["main:coreutils-env"]="9.5-r2"
    ["main:coreutils-fmt"]="9.5-r2"
    ["main:coreutils-sha512sum"]="9.5-r2"
    ["main:bash"]="5.2.37-r0"
    ["main:dash"]="0.5.12-r2"
    ["main:findutils"]="4.10.0-r0"
    ["main:diffutils"]="3.10-r0"
    ["main:grep"]="3.11-r0"
    ["main:sed"]="4.9-r2"
    ["main:gawk"]="5.3.1-r0"
    ["main:gmp"]="6.3.0-r2"
    ["main:readline"]="8.2.13-r0"
    ["main:libncursesw"]="6.5_p20241006-r3"
    ["main:ncurses-terminfo-base"]="6.5_p20241006-r3"
    ["main:lua5.4"]="5.4.7-r0"
    ["main:lua5.4-libs"]="5.4.7-r0"
    ["main:jq"]="1.7.1-r0"
    ["main:oniguruma"]="6.9.9-r0"
    ["main:sqlite"]="3.48.0-r4"
    ["main:sqlite-libs"]="3.48.0-r4"
    ["main:tree"]="2.2.1-r0"
)

# Subset whose binaries are exposed as standalone "static-bins" suite paths.
# Most are dynamic but link only against musl/zlib/etc., already in rootfs/.
# Applet list (hardcoded — busybox 1.37 inventory).  Busybox does not have
# b2sum / numfmt / base32; those tests fall through to the dynamic-coreutils
# suite where the real coreutils binary is available.
STATIC_APPLETS=(
    echo cat head tail wc sort tr seq expr factor base64 md5sum sha256sum
    cp touch ls stat basename dirname realpath df du
    uname date id printenv nproc
    true false sleep env nice nohup timeout
    sha1sum sha512sum cksum
    chmod chown ln rm mkdir rmdir mv pwd
    cmp diff find sed grep awk
)

MINIROOTFS_TGZ="alpine-minirootfs-${ALPINE_PATCH}-${ALPINE_ARCH}.tar.gz"

c_blue()
{
    printf '\033[0;34m%s\033[0m' "$*"
}
c_green()
{
    printf '\033[0;32m%s\033[0m' "$*"
}
c_yellow()
{
    printf '\033[1;33m%s\033[0m' "$*"
}

log()
{
    printf '%s %s\n' "$(c_blue ' fixtures:')" "$*"
}
ok()
{
    printf '%s %s\n' "$(c_green '       ok:')" "$*"
}

fetch()
{
    local url="$1" dest="$2"
    if [ -s "$dest" ] && [ "${FORCE:-0}" != "1" ]; then
        return 0
    fi
    log "fetch $(basename "$dest")"
    curl -fsSL --retry 3 -o "$dest.partial" "$url"
    mv "$dest.partial" "$dest"
}

apk_url()
{
    local repo="$1" name="$2" version="$3"
    case "$repo" in
        main) echo "${MAIN_REPO}/${name}-${version}.apk" ;;
        community) echo "${COMMUNITY_REPO}/${name}-${version}.apk" ;;
        *)
            echo "unknown repo: $repo" >&2
            return 1
            ;;
    esac
}

apk_path()
{
    local name="$1" version="$2"
    echo "${CACHE}/${name}-${version}.apk"
}

# Strip Alpine apk metadata (.PKGINFO, .SIGN.*, .pre-install, etc.) when
# extracting into a target tree.  These are not real files and pollute the
# rootfs.
extract_apk_to()
{
    local apk="$1" dest="$2"
    mkdir -p "$dest"
    tar xzf "$apk" -C "$dest" \
        --exclude='.PKGINFO' \
        --exclude='.SIGN.*' \
        --exclude='.pre-install' \
        --exclude='.post-install' \
        --exclude='.pre-upgrade' \
        --exclude='.post-upgrade' \
        --exclude='.pre-deinstall' \
        --exclude='.post-deinstall' \
        --exclude='.trigger' 2> /dev/null || true
}

main()
{
    mkdir -p "$CACHE" "$KERNEL_DIR" "$KEYS_DIR" "$STATICBIN" "$ROOTFS"

    # Download all required apk packages.
    for key in "${!PKGS[@]}"; do
        local repo="${key%%:*}" name="${key##*:}" version="${PKGS[$key]}"
        fetch "$(apk_url "$repo" "$name" "$version")" "$(apk_path "$name" "$version")"
    done

    fetch "${RELEASES}/${MINIROOTFS_TGZ}" "${CACHE}/${MINIROOTFS_TGZ}"

    # Stage the rootfs.
    if [ "${FORCE:-0}" = "1" ] || [ ! -e "${ROOTFS}/.staged" ]; then
        log "stage rootfs"
        rm -rf "$ROOTFS"
        mkdir -p "$ROOTFS"
        tar xzf "${CACHE}/${MINIROOTFS_TGZ}" -C "$ROOTFS" 2> /dev/null

        # Overlay every cached apk except linux-virt (kernel goes elsewhere).
        # The kernel apk's lib/modules/ tree IS overlayed (needed for modprobe).
        for key in "${!PKGS[@]}"; do
            local name="${key##*:}" version="${PKGS[$key]}"
            [ "$name" = "linux-virt" ] && continue
            extract_apk_to "$(apk_path "$name" "$version")" "$ROOTFS"
        done

        # Extract just the kernel-modules subtree from linux-virt.
        local modstage
        modstage="$(mktemp -d)"
        tar xzf "$(apk_path linux-virt "${PKGS["main:linux-virt"]}")" \
            -C "$modstage" 'lib/modules' 2> /dev/null
        cp -R "$modstage/lib/modules" "$ROOTFS/lib/" 2> /dev/null
        rm -rf "$modstage"

        # /init (custom — no openrc, just bring up minimum services for ssh).
        cat > "${ROOTFS}/init" << 'EOF'
#!/bin/sh
# Custom init — sets up enough for dropbear ssh + 9p shared mounts.
set +e
exec </dev/console >/dev/console 2>&1

mount -t proc proc /proc
mount -t sysfs sys /sys
mount -t devtmpfs dev /dev
mkdir -p /dev/pts /dev/shm
mount -t devpts -o gid=5,mode=620 devpts /dev/pts
mount -t tmpfs tmpfs /dev/shm
mount -t tmpfs tmpfs /run

# Load kernel modules for virtio-net + 9p shared filesystem.  Order matters:
# bus/transport modules first, then class drivers.
for mod in failover net_failover virtio_net netfs 9pnet 9pnet_virtio 9p; do
    modprobe "$mod" 2>/dev/null || true
done

# Shared filesystem from the host (qemu -virtfs).
mkdir -p /mnt/host
mount -t 9p -o trans=virtio,version=9p2000.L,msize=1048576 host /mnt/host \
    || echo "qemu-runner: 9p mount failed"

# Networking: qemu user-mode places guest at 10.0.2.15, host at 10.0.2.2.
ifconfig lo 127.0.0.1 up
ifconfig eth0 10.0.2.15 netmask 255.255.255.0 up || echo "qemu-runner: eth0 up failed"
route add default gw 10.0.2.2 2>/dev/null

# Dropbear — pre-baked host keys, pubkey auth only (passwords disabled).
mkdir -p /etc/dropbear /var/empty /var/log
chmod 700 /root /root/.ssh
chown -R 0:0 /root
[ -f /etc/dropbear/dropbear_ed25519_host_key ] || \
    /usr/bin/dropbearkey -t ed25519 -f /etc/dropbear/dropbear_ed25519_host_key >/dev/null 2>&1
[ -f /etc/dropbear/dropbear_rsa_host_key ] || \
    /usr/bin/dropbearkey -t rsa -s 2048 -f /etc/dropbear/dropbear_rsa_host_key >/dev/null 2>&1

echo "qemu-runner: ready"
# -s: disable password auth (pubkey only); -F: foreground; -E: stderr logs.
exec /usr/sbin/dropbear -F -E -s -p 22 \
    -r /etc/dropbear/dropbear_rsa_host_key \
    -r /etc/dropbear/dropbear_ed25519_host_key
EOF
        chmod 755 "${ROOTFS}/init"

        # Hostname & basic config
        echo "elfuse-qemu" > "${ROOTFS}/etc/hostname"
        cat > "${ROOTFS}/etc/hosts" << 'EOF'
127.0.0.1 localhost elfuse-qemu
10.0.2.15 elfuse-qemu
EOF

        # Need a tty entry for dropbear's PAM-equivalent path.
        grep -q '^root:' "${ROOTFS}/etc/passwd" \
            || echo 'root:x:0:0:root:/root:/bin/sh' >> "${ROOTFS}/etc/passwd"

        touch "${ROOTFS}/.staged"
    fi
    ok "rootfs ready ($(du -sh "$ROOTFS" | cut -f1))"

    # Generate the SSH keypair if needed.
    if [ ! -s "${KEYS_DIR}/ssh_key" ]; then
        log "generate ssh keypair"
        ssh-keygen -t ed25519 -N '' -C 'elfuse-qemu-runner' \
            -f "${KEYS_DIR}/ssh_key" > /dev/null
    fi

    # Install pubkey into rootfs/root/.ssh/authorized_keys
    mkdir -p "${ROOTFS}/root/.ssh"
    cp "${KEYS_DIR}/ssh_key.pub" "${ROOTFS}/root/.ssh/authorized_keys"
    chmod 700 "${ROOTFS}/root/.ssh"
    chmod 600 "${ROOTFS}/root/.ssh/authorized_keys"
    ok "ssh keypair installed"

    # Extract the kernel from linux-virt.
    if [ ! -s "${KERNEL_DIR}/vmlinuz-virt" ] || [ "${FORCE:-0}" = "1" ]; then
        log "extract kernel"
        rm -rf "${KERNEL_DIR}/work"
        mkdir -p "${KERNEL_DIR}/work"
        tar xzf "$(apk_path linux-virt "${PKGS["main:linux-virt"]}")" \
            -C "${KERNEL_DIR}/work" boot/vmlinuz-virt 2> /dev/null
        mv "${KERNEL_DIR}/work/boot/vmlinuz-virt" "${KERNEL_DIR}/vmlinuz-virt"
        rm -rf "${KERNEL_DIR}/work"
    fi
    ok "kernel: ${KERNEL_DIR}/vmlinuz-virt"

    # Build the initramfs archive.
    if [ ! -s "$INITRAMFS" ] || [ "$ROOTFS/.staged" -nt "$INITRAMFS" ]; then
        log "build initramfs"
        (cd "$ROOTFS" && find . -print0 | LC_ALL=C sort -z \
            | cpio --quiet --null -o -H newc 2> /dev/null | gzip -9) > "$INITRAMFS"
    fi
    ok "initramfs: $(du -h "$INITRAMFS" | cut -f1)"

    # Stage the dynamic-bin aggregate directory.
    # Single flat directory of *relative* symlinks pointing into the rootfs.
    # Relative paths matter: this same tree is consumed by elfuse on macOS
    # AND by qemu's guest kernel after a 9p mount, where any absolute host
    # path would no longer resolve.
    local dynbin="${FIXTURES}/aarch64-musl/dyn-bin"
    if [ ! -d "$dynbin" ] || [ "${FORCE:-0}" = "1" ]; then
        log "stage dyn-bin aggregate"
        rm -rf "$dynbin"
        mkdir -p "$dynbin"
        # ${dynbin} is at <fixtures>/aarch64-musl/dyn-bin; rootfs is at
        # <fixtures>/rootfs.  The relative path back is "../../rootfs/...".
        for sub in bin usr/bin; do
            [ -d "${ROOTFS}/${sub}" ] || continue
            for src in "${ROOTFS}/${sub}"/*; do
                [ -e "$src" ] || continue
                local name
                name="$(basename "$src")"
                [ -e "${dynbin}/${name}" ] && continue
                ln -sfn "../../rootfs/${sub}/${name}" "${dynbin}/${name}"
            done
        done
    fi
    ok "dyn-bin: $(find "$dynbin" -maxdepth 1 -type l 2> /dev/null | wc -l | tr -d ' ') entries"

    # Stage the static-bin tree.
    if [ ! -s "${STATICBIN}/busybox" ] || [ "${FORCE:-0}" = "1" ]; then
        log "stage static-bin tree"
        rm -rf "${STATICBIN}"
        mkdir -p "${STATICBIN}"
        local stage
        stage="$(mktemp -d)"
        tar xzf "$(apk_path busybox-static "${PKGS["main:busybox-static"]}")" -C "$stage" 2> /dev/null
        mv "${stage}/bin/busybox.static" "${STATICBIN}/busybox"
        chmod 755 "${STATICBIN}/busybox"
        rm -rf "$stage"
        for applet in "${STATIC_APPLETS[@]}"; do
            ln -sfn busybox "${STATICBIN}/${applet}"
        done
    fi
    ok "static-bin: ${STATICBIN}/busybox + ${#STATIC_APPLETS[@]} applets"

    printf '\n%s\n' "$(c_yellow 'Fixtures ready.')"
    printf 'rootfs/sysroot:  %s\n' "$ROOTFS"
    printf 'kernel:          %s\n' "${KERNEL_DIR}/vmlinuz-virt"
    printf 'initramfs:       %s\n' "$INITRAMFS"
    printf 'ssh key:         %s\n' "${KEYS_DIR}/ssh_key"
    printf 'static bin tree: %s\n' "$STATICBIN"
}

main "$@"
