#!/usr/bin/env bash
# qemu-runner.sh — Boot qemu-system-aarch64 with the elfuse test fixtures
# initramfs and provide qemu_exec() for command execution over ssh.
#
# Sourced by tests/test-matrix.sh (for the qemu-aarch64 mode) but also
# usable interactively:
#   . tests/qemu-runner.sh
#   qemu_start
#   qemu_exec uname -a
#   qemu_stop
#
# The booted VM mounts the host repo root at /mnt/host via virtio-9p,
# so any path under the repo is reachable from the VM as
# /mnt/host/<relative-path>.
#
# SPDX-License-Identifier: Apache-2.0

# Resolve repo paths relative to this script's location.
_QR_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
_QR_FIX="${_QR_DIR}/externals/test-fixtures"

QEMU_BIN="${QEMU_BIN:-qemu-system-aarch64}"
QEMU_PORT="${QEMU_PORT:-2222}"
QEMU_MEM="${QEMU_MEM:-2048}"
QEMU_SMP="${QEMU_SMP:-4}"
# Accelerator + CPU model are auto-selected at qemu_start time: HVF + cpu=host
# on Apple Silicon when the qemu build supports it, otherwise TCG + cortex-a72
# (slower but portable to non-aarch64 hosts and qemu builds without HVF).
# Either can be forced via QEMU_ACCEL=tcg / QEMU_CPU=cortex-a72.
QEMU_ACCEL="${QEMU_ACCEL:-}"
QEMU_CPU="${QEMU_CPU:-}"
QEMU_BOOT_TIMEOUT="${QEMU_BOOT_TIMEOUT:-90}"
QEMU_SSH_KEY="${QEMU_SSH_KEY:-${_QR_FIX}/keys/ssh_key}"
QEMU_KERNEL="${QEMU_KERNEL:-${_QR_FIX}/kernel/vmlinuz-virt}"
QEMU_INITRD="${QEMU_INITRD:-${_QR_FIX}/initramfs.cpio.gz}"
QEMU_SHARE_PATH="${QEMU_SHARE_PATH:-${_QR_DIR}}"

_QR_PIDFILE=""
_QR_LOG=""
_QR_CTL=""

# Fixture path inside the VM (always /mnt/host/<relative>).
qemu_guestpath()
{
    local p="$1"
    case "$p" in
        "${_QR_DIR}"/*) echo "/mnt/host/${p#${_QR_DIR}/}" ;;
        /*) echo "$p" ;;
        *) echo "/mnt/host/$p" ;;
    esac
}

# Verify fixtures are present; fetch on demand if not.
qemu_ensure_fixtures()
{
    if [ ! -s "$QEMU_KERNEL" ] || [ ! -s "$QEMU_INITRD" ] || [ ! -s "$QEMU_SSH_KEY" ]; then
        echo "qemu-runner: fixtures missing, running tests/fetch-fixtures.sh"
        bash "${_QR_DIR}/tests/fetch-fixtures.sh" >&2
    fi
    for f in "$QEMU_KERNEL" "$QEMU_INITRD" "$QEMU_SSH_KEY"; do
        [ -s "$f" ] || {
            echo "qemu-runner: missing $f" >&2
            return 1
        }
    done
}

# Pick a free localhost TCP port for ssh forwarding (avoids collisions
# when several test-matrix runs overlap or QEMU_PORT is in use).
qemu_pick_port()
{
    if command -v python3 > /dev/null 2>&1; then
        python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()'
    else
        echo "$QEMU_PORT"
    fi
}

qemu_pick_accel()
{
    if [ -n "$QEMU_ACCEL" ]; then
        return
    fi
    if "$QEMU_BIN" -accel help 2> /dev/null | grep -qx hvf; then
        QEMU_ACCEL=hvf
    else
        QEMU_ACCEL=tcg
    fi
}

qemu_pick_cpu()
{
    [ -n "$QEMU_CPU" ] && return
    case "$QEMU_ACCEL" in
        hvf) QEMU_CPU=host ;;
        *) QEMU_CPU=cortex-a72 ;;
    esac
}

qemu_start()
{
    qemu_ensure_fixtures || return 1

    if [ -n "$_QR_PIDFILE" ] && [ -s "$_QR_PIDFILE" ]; then
        echo "qemu-runner: already started (pid=$(cat "$_QR_PIDFILE"))" >&2
        return 0
    fi

    command -v "$QEMU_BIN" > /dev/null 2>&1 || {
        echo "qemu-runner: $QEMU_BIN not found in PATH" >&2
        return 1
    }

    qemu_pick_accel
    qemu_pick_cpu

    local rundir
    rundir="$(mktemp -d -t elfuse-qemu.XXXXXX)"
    _QR_PIDFILE="${rundir}/qemu.pid"
    _QR_LOG="${rundir}/qemu-serial.log"
    _QR_CTL="${rundir}/ssh-ctl"

    QEMU_PORT="$(qemu_pick_port)"

    "$QEMU_BIN" \
        -machine virt -accel "$QEMU_ACCEL" -cpu "$QEMU_CPU" \
        -m "$QEMU_MEM" -smp "$QEMU_SMP" \
        -kernel "$QEMU_KERNEL" -initrd "$QEMU_INITRD" \
        -append "console=ttyAMA0 panic=5 quiet" \
        -netdev "user,id=net0,hostfwd=tcp:127.0.0.1:${QEMU_PORT}-:22" \
        -device virtio-net-pci,netdev=net0 \
        -fsdev "local,id=share,path=${QEMU_SHARE_PATH},security_model=none,readonly=on" \
        -device virtio-9p-pci,fsdev=share,mount_tag=host \
        -nographic -display none -no-reboot -monitor none \
        -serial "file:${_QR_LOG}" \
        -pidfile "$_QR_PIDFILE" \
        > /dev/null 2>&1 &
    disown

    # Wait for ssh port to come up.

    for _ in $(seq 1 "$QEMU_BOOT_TIMEOUT"); do
        if (echo > /dev/tcp/127.0.0.1/"$QEMU_PORT") 2> /dev/null; then
            break
        fi
        sleep 1
    done
    if ! (echo > /dev/tcp/127.0.0.1/"$QEMU_PORT") 2> /dev/null; then
        echo "qemu-runner: VM did not boot within ${QEMU_BOOT_TIMEOUT}s" >&2
        qemu_stop
        return 1
    fi

    # Wait for dropbear to fully accept; first connection often races boot.
    for _ in $(seq 1 30); do
        if _qemu_ssh_raw -o ConnectTimeout=2 'echo ready' > /dev/null 2>&1; then
            break
        fi
        sleep 1
    done
}

# Each call opens a fresh ssh connection.  Avoids ControlMaster pitfalls
# (master dying mid-suite cascades rc=255 to every later command) at the
# cost of ~100ms handshake overhead per call — a flat ~15s across the
# full matrix, well within the suite's tolerance.
_qemu_ssh_raw()
{
    ssh -o StrictHostKeyChecking=no \
        -o UserKnownHostsFile=/dev/null \
        -o LogLevel=ERROR \
        -o BatchMode=yes \
        -o ConnectTimeout=10 \
        -o ServerAliveInterval=10 \
        -o ServerAliveCountMax=6 \
        -i "$QEMU_SSH_KEY" \
        -p "$QEMU_PORT" \
        root@127.0.0.1 "$@"
}

# Run a command in the VM.  Any argument that is an absolute path under the
# host repo root is rewritten to its /mnt/host/... in-VM equivalent so the
# guest can dereference it through the virtio-9p share.  Cwd is forced to
# /mnt/host so test arguments using paths like "tests/foo" work the same
# as they do when run on the macOS host.
qemu_exec()
{
    local args=() a
    for a in "$@"; do
        case "$a" in
            "${_QR_DIR}"/*) args+=("$(qemu_guestpath "$a")") ;;
            *) args+=("$a") ;;
        esac
    done
    local quoted
    printf -v quoted '%q ' "${args[@]}"
    _qemu_ssh_raw "cd /mnt/host && ${quoted}"
}

qemu_stop()
{
    if [ -n "$_QR_PIDFILE" ] && [ -s "$_QR_PIDFILE" ]; then
        local pid
        pid=$(cat "$_QR_PIDFILE" 2> /dev/null)
        if [ -n "$pid" ] && kill -0 "$pid" 2> /dev/null; then
            kill "$pid" 2> /dev/null
            # give qemu time to exit cleanly; force-kill if it lingers

            for _ in 1 2 3 4 5; do
                kill -0 "$pid" 2> /dev/null || break
                sleep 1
            done
            kill -0 "$pid" 2> /dev/null && kill -9 "$pid" 2> /dev/null
        fi
    fi
    if [ -n "$_QR_PIDFILE" ]; then
        rm -rf "$(dirname "$_QR_PIDFILE")"
    fi
    _QR_PIDFILE=""
    _QR_LOG=""
    _QR_CTL=""
}

# When sourced, register a cleanup trap that does not clobber the caller's
# existing trap chain.  When executed directly, the EXIT trap fires on
# script exit.
trap 'qemu_stop' EXIT

# CLI driver: when run directly, support `qemu-runner.sh start|exec|stop`.
if [ "${BASH_SOURCE[0]:-$0}" = "$0" ]; then
    cmd="${1:-help}"
    shift || true
    case "$cmd" in
        start)
            qemu_start
            echo "PORT=$QEMU_PORT KEY=$QEMU_SSH_KEY"
            ;;
        exec)
            qemu_start
            qemu_exec "$@"
            ;;
        stop)
            qemu_stop
            ;;
        *)
            cat << EOF
Usage: $0 <start|exec ARGS...|stop>

Boots qemu-system-aarch64 with the test fixtures and exposes ssh.
The host repo is shared into the VM at /mnt/host (read-only).

Environment overrides:
  QEMU_BIN, QEMU_MEM, QEMU_SMP, QEMU_CPU,
  QEMU_PORT (initial; may be re-picked if busy),
  QEMU_BOOT_TIMEOUT, QEMU_SSH_KEY, QEMU_KERNEL, QEMU_INITRD.
EOF
            ;;
    esac
fi
