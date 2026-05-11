# elfuse

`elfuse` runs aarch64-linux ELF binaries on macOS Apple Silicon through
Apple's Hypervisor.framework. It is a process-scoped Linux user-space runtime:
guest code executes on the CPU inside a lightweight VM, while Linux syscalls
are intercepted and translated to macOS behavior in host-side handlers.

This is not a container engine and not a general-purpose Linux kernel. It is a
focused compatibility layer for running Linux user-space workloads directly from
the macOS shell, with support for static binaries, dynamic loaders via
`--sysroot`, guest threads, process management, signals, `/proc` emulation, and
guest debugging through a built-in GDB RSP stub.

## Highlights

- Native Apple Silicon execution through Hypervisor.framework
- Static and dynamically linked `aarch64-linux` ELF binaries
- Linux-style processes, threads, signals, timers, futexes, and polling
- Synthetic `/proc` and selected `/dev` emulation for user-space probes
- Built-in GDB Remote Serial Protocol stub usable from `gdb` or `lldb`
- Self-contained test matrix that cross-checks elfuse against QEMU

## Requirements

- macOS on Apple Silicon
- macOS 13 or newer
- Xcode Command Line Tools, `clang`, `codesign`, and GNU `make`
- GNU `objcopy` from Homebrew `binutils`, or `llvm-objcopy`
- Hypervisor entitlement: `com.apple.security.hypervisor`

For guest test binaries, the project also expects an AArch64 Linux cross
toolchain. The default paths in `mk/toolchain.mk` target the toolchain layout
used by the repository test harness, but `CROSS_COMPILE` and
`BAREMETAL_CROSS` are overridable.

To run `make check`, install the Homebrew AArch64 embedded toolchain first:

```sh
brew install --cask gcc-aarch64-embedded
```

## Quick Start

```sh
git clone https://github.com/sysprog21/elfuse
cd elfuse
make elfuse
make test-busybox
build/elfuse build/busybox
```
Replace `build/busybox` with Arm64/Linux executable files.

For dynamically linked guests:

```sh
build/elfuse --sysroot /path/to/sysroot ./path/to/program
```

For early debugging:

```sh
build/elfuse --gdb 1234 --gdb-stop-on-entry ./path/to/program
```

The build signs `build/elfuse` before use. Override the signing identity with
`SIGN_IDENTITY="Developer ID ..."` when needed.

## Documentation

- [docs/usage.md](docs/usage.md): command-line options, dynamic linking via
  `--sysroot`, and attaching `gdb` / `lldb` to the built-in stub.
- [docs/testing.md](docs/testing.md): build prerequisites, the `make check`
  flow, the QEMU cross-check matrix, and fixture handling.
- [docs/internals.md](docs/internals.md): canonical technical reference —
  HVF constraints, EL1 shim and HVC protocol, page-table splitting, syscall
  translation tables, threads/futex, fork/clone IPC, signals, ptrace, and
  the GDB stub.

## Build And Validation

Most common targets:

```sh
make elfuse        # build and codesign build/elfuse
make check         # quick unit suite + BusyBox applet smoke
make test-gdbstub  # debugger integration
make test-matrix   # cross-check elfuse against QEMU on the same corpus
make lint          # clang-tidy
```

`make check` is the recommended pre-commit gate. `make test-matrix` is the
recommended gate for changes touching procfs, dynamic linking, networking,
or process semantics. See [docs/testing.md](docs/testing.md) for the full
target list, fixture flow, and validation-by-change-type guidance.

## Scope And Limitations

`elfuse` targets pragmatic Linux user-space compatibility. Supported areas
include ELF and dynamic-loader bootstrap, sysroot-aware path translation,
Linux-style FD semantics, `fork` / `clone` / `execve` / `wait*` / ptrace,
signals and timers, polling families (`epoll`, `eventfd`, `signalfd`,
`timerfd`, `inotify`), sockets and netlink, and synthetic `/proc`, `/dev`,
and `/proc/net/*` views sufficient for tools such as BusyBox `ps`, `uptime`,
and `top`.

Boundaries to be aware of:

- The target is Linux user-space ABI compatibility, not kernel
  virtualization. `/proc`, `/dev`, and mount data are compatibility views.
- HVF allows one VM per host process, so Linux-style `fork` is implemented
  via `posix_spawn` plus state transfer (a fast CoW path is used when
  available — see [docs/internals.md](docs/internals.md)).
- `MAP_SHARED` is treated as `MAP_PRIVATE`; this matches single-process
  guest semantics and unblocks tools that expect file-backed mappings.
- Unsupported syscalls return Linux-style errors rather than silently
  succeeding.

## License

Apache License 2.0. See [LICENSE](LICENSE).

Copyright 2026 elfuse contributors  
Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
