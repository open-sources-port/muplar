# Building And Testing

This document describes the development toolchain, the main `make` targets, and
how the repository validation flow is structured.

## Build Requirements

Host build requirements:

- Apple Silicon macOS host
- Xcode Command Line Tools
- `clang`
- `codesign`
- GNU `make`
- GNU `objcopy` or `llvm-objcopy`

Guest test builds additionally require:

- An AArch64 Linux cross-compiler for C test programs
- An AArch64 bare-metal toolchain for the assembly smoke test

The repository defaults are defined in `mk/toolchain.mk`, but these variables
are intended to be overridden when needed:

- `CROSS_COMPILE`
- `BAREMETAL_CROSS`
- `SIGN_IDENTITY`

## Main Targets

The most useful development targets are:

```sh
make elfuse
make check
make test-gdbstub
make test-matrix
make lint
make clean
```

What they do:

- `make elfuse`: build and sign `build/elfuse`
- `make check`: unit suite from `tests/manifest.txt` followed by the BusyBox
  applet smoke suite. The BusyBox binary is auto-resolved from
  `externals/test-fixtures/aarch64-musl/staticbin/bin/busybox` if present, or
  downloaded into `build/busybox` on first run.
- `make test-busybox`: just the BusyBox suite, useful when iterating on a
  single applet failure without rerunning the unit suite
- `make test-gdbstub`: debugger integration checks against the built-in GDB stub
- `make test-matrix`: broader `elfuse` and QEMU cross-check
- `make lint`: static analysis through `clang-tidy`

## Quick Iteration

For normal code changes:

```sh
make elfuse
make check
```

For changes that touch procfs, path handling, networking, dynamic linking, or
guest process semantics, run the matrix as well:

```sh
make test-matrix
```

`make check` already runs the BusyBox applet suite as a second stage, so a
green `make check` covers BusyBox validation. Use `make test-busybox` to
iterate on a single applet failure without rerunning the unit suite.

## Test Matrix

The matrix driver lives in `tests/test-matrix.sh`. It runs the same guest test
corpus in two execution modes:

- `elfuse-aarch64`: every binary is executed via `build/elfuse` on macOS
- `qemu-aarch64`: the same binaries run natively inside an Alpine
  `aarch64-linux-musl` minirootfs booted by `qemu-system-aarch64`

The goal is not to compare performance. The goal is to compare guest-observable
behavior against a ground-truth Linux AArch64 environment so that any divergence
in syscall translation, procfs emulation, or process semantics is caught early.

Run a single mode with `bash tests/test-matrix.sh elfuse-aarch64` or
`bash tests/test-matrix.sh qemu-aarch64`; `all` runs both back-to-back.

Fixture handling is self-contained:

- On first use, `tests/fetch-fixtures.sh` downloads the required Alpine
  packages and the `linux-virt` kernel into `externals/test-fixtures/` and
  assembles an initramfs. Subsequent runs are zero-config.
- The same fixture tree is reused by both matrix modes.
- QEMU mode requires `qemu-system-aarch64` on `PATH` (Homebrew `qemu`
  provides it).
- musl is the only Alpine libc; the glibc-dynamic suite is skipped unless
  `GUEST_GLIBC_*` environment variables point at an external sysroot.

## Test Inventory

The repository contains several layers of validation:

- unit-style guest tests compiled from `tests/*.c`
- shell integration suites such as BusyBox, coreutils, and dynamic-loader tests
- debugger integration tests for the GDB stub
- native macOS HVF checks such as multi-vCPU and RWX validation

The quick suite is driven by `tests/driver.sh`, which supports:

- `-f PATTERN` to filter tests
- `-l` to list them
- `-T` for TAP output

Example:

```sh
bash tests/driver.sh -f test-proc
```

## Validation Strategy By Change Type

Suggested minimum validation:

| Change area | Recommended validation |
|-------------|------------------------|
| CLI, logging, docs-only build rules | `make elfuse` |
| General syscall or runtime logic | `make elfuse && make check` |
| `/proc`, `/dev`, path, or BusyBox-sensitive behavior | `make elfuse && make check` |
| Broad behavioral changes | `make elfuse && make check && make test-matrix` |
| Debugger or ptrace flow | `make elfuse && make test-gdbstub` |
