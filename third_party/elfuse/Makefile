# elfuse — aarch64-linux ELF executor on macOS Apple Silicon
#
# Copyright 2026 elfuse contributors
# SPDX-License-Identifier: Apache-2.0
#
# Usage:
#   make <target> [SIGN_IDENTITY="Your Signing Identity"]
#
# Example: make elfuse
#          make test-hello
#          make V=1 elfuse    (verbose — show full commands)

.DEFAULT_GOAL := help
.DELETE_ON_ERROR:

include mk/toolchain.mk
include mk/config.mk

# Source files.
SRCS := \
    main.c \
    core/guest.c \
    core/elf.c \
    core/stack.c \
    core/vdso.c \
    core/bootstrap.c \
    runtime/thread.c \
    runtime/futex.c \
    runtime/forkipc.c \
    runtime/fork-state.c \
    runtime/procemu.c \
    runtime/proctitle.c \
    syscall/syscall.c \
    syscall/fdtable.c \
    syscall/translate.c \
    syscall/mem.c \
    syscall/path.c \
    syscall/fs.c \
    syscall/fs-stat.c \
    syscall/fs-xattr.c \
    syscall/io.c \
    syscall/poll.c \
    syscall/fd.c \
    syscall/inotify.c \
    syscall/time.c \
    syscall/sys.c \
    syscall/proc.c \
    syscall/proc-identity.c \
    syscall/proc-pidfd.c \
    syscall/proc-state.c \
    syscall/exec.c \
    syscall/signal.c \
    syscall/net.c \
    syscall/net-msg.c \
    syscall/net-abi.c \
    syscall/net-absock.c \
    syscall/net-sockopt.c \
    syscall/netlink.c \
    syscall/sysvipc.c \
    debug/crashreport.c \
    debug/gdbstub.c \
    debug/gdbstub-reg.c \
    debug/gdbstub-rsp.c \
    debug/log.c

SRCS := $(addprefix src/,$(SRCS))
OBJS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRCS))

DISPATCH_MANIFEST := src/syscall/dispatch.tbl
DISPATCH_GENERATOR := scripts/gen-syscall-dispatch.py
DISPATCH_HEADER := $(BUILD_DIR)/dispatch.h
HVF_LDFLAGS := -framework Hypervisor -arch arm64

# Generated headers under build/ that must exist before compiling sources that
# include them.
GENERATED_HEADERS := $(BUILD_DIR)/shim_blob.h $(BUILD_DIR)/version.h $(DISPATCH_HEADER)

include mk/common.mk
include mk/shim.mk

define link-and-sign
	@echo "  LD      $1"
	$(Q)tmp="$1.$$$$.tmp"; \
	$(CC) $(CFLAGS) -o "$$tmp" $2 $(HVF_LDFLAGS); \
	echo "  SIGN    $1"; \
	codesign --entitlements $(ENTITLEMENTS) -f -s "$(SIGN_IDENTITY)" "$$tmp"; \
	mv "$$tmp" "$1"
endef

# ── Main executable ──────────────────────────────────────────────
.PHONY: all elfuse
.PHONY: gen-syscall-dispatch check-syscall-dispatch

all: elfuse

## Regenerate build/dispatch.h from src/syscall/dispatch.tbl
gen-syscall-dispatch:
	@python3 $(DISPATCH_GENERATOR)

## Verify build/dispatch.h matches the generator output
check-syscall-dispatch: $(DISPATCH_HEADER)
	@python3 $(DISPATCH_GENERATOR) --check

$(DISPATCH_HEADER): $(DISPATCH_MANIFEST) $(DISPATCH_GENERATOR) src/syscall/abi.h | $(BUILD_DIR)
	@echo "  GEN     $@"
	$(Q)tmp="$@.$$$$.tmp"; \
	python3 $(DISPATCH_GENERATOR) --output "$$tmp"; \
	cmp -s "$$tmp" "$@" 2>/dev/null || mv "$$tmp" "$@"; \
	rm -f "$$tmp"

$(BUILD_DIR)/syscall/syscall.o: $(DISPATCH_HEADER)

## Build the elfuse executable
elfuse: $(ELFUSE_BIN)

$(ELFUSE_BIN): $(OBJS) | $(BUILD_DIR)
	$(call link-and-sign,$@,$(OBJS))

# ── Native test binaries (macOS, Hypervisor.framework) ───────────

## Build the multi-vCPU HVF validation test (native macOS binary)
$(BUILD_DIR)/test-multi-vcpu: $(BUILD_DIR)/test-multi-vcpu.o | $(BUILD_DIR)
	$(call link-and-sign,$@,$<)

## Build the RWX W^X validation test (native macOS binary)
$(BUILD_DIR)/test-rwx: $(BUILD_DIR)/test-rwx.o | $(BUILD_DIR)
	$(call link-and-sign,$@,$<)

# ── Guest test binaries (cross-compiled, aarch64-linux) ──────────
# Only used when GUEST_TEST_BINARIES is not set.

ifndef GUEST_TEST_BINARIES
$(BUILD_DIR)/test-hello: tests/hello.S tests/simple.ld | $(BUILD_DIR)
	@echo "  AS      tests/hello.S"
	$(Q)$(BAREMETAL_CROSS)as -o $(BUILD_DIR)/test-hello.o tests/hello.S
	@echo "  LD      $@"
	$(Q)$(BAREMETAL_CROSS)ld -T tests/simple.ld -o $@ $(BUILD_DIR)/test-hello.o

# Pattern rule: cross-compile tests/*.c to static aarch64-linux binaries
# -D_GNU_SOURCE exposes pipe2/dup3/O_DIRECT/etc. on glibc (musl exposes them by default)
$(BUILD_DIR)/%: tests/%.c | $(BUILD_DIR)
	@echo "  CROSS   $<"
	$(Q)$(CROSS_COMPILE)gcc -D_GNU_SOURCE -static -O2 -o $@ $<

# test-pthread needs -lpthread
$(BUILD_DIR)/test-pthread: tests/test-pthread.c | $(BUILD_DIR)
	@echo "  CROSS   $< (with -lpthread)"
	$(Q)$(CROSS_COMPILE)gcc -D_GNU_SOURCE -static -O2 -o $@ $< -lpthread

# test-signalfd-hardening needs -lpthread for the worker-thread tid
# regression case in test_rt_sigqueueinfo_rejects_thread_tid.
$(BUILD_DIR)/test-signalfd-hardening: tests/test-signalfd-hardening.c | $(BUILD_DIR)
	@echo "  CROSS   $< (with -lpthread)"
	$(Q)$(CROSS_COMPILE)gcc -D_GNU_SOURCE -static -O2 -o $@ $< -lpthread

# test-futex-waitv needs -lpthread for the host wake-thread used to unblock
# the main thread's futex_waitv.
$(BUILD_DIR)/test-futex-waitv: tests/test-futex-waitv.c | $(BUILD_DIR)
	@echo "  CROSS   $< (with -lpthread)"
	$(Q)$(CROSS_COMPILE)gcc -D_GNU_SOURCE -static -O2 -o $@ $< -lpthread

endif

include mk/tests.mk
include mk/analysis.mk
include mk/help.mk
