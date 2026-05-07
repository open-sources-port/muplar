# Project configuration

ENTITLEMENTS := entitlements.plist
SIGN_IDENTITY ?= -
BUILD_DIR := build
ELFUSE_BIN := $(BUILD_DIR)/elfuse
VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo "unknown")

# Test binary directory: either pre-built via GUEST_TEST_BINARIES,
# auto-detected from build/bin, or locally cross-compiled via $(CROSS_COMPILE)gcc.
ifeq ($(origin GUEST_TEST_BINARIES), undefined)
  ifneq ($(wildcard $(BUILD_DIR)/bin/test-hello),)
    GUEST_TEST_BINARIES := $(BUILD_DIR)
  endif
endif

# Exclude native macOS test files from cross-compilation
NATIVE_TESTS := tests/test-multi-vcpu.c tests/test-rwx.c

ifdef GUEST_TEST_BINARIES
  TEST_DIR  := $(GUEST_TEST_BINARIES)/bin
  TEST_DEPS :=
  TEST_HELLO_DEP :=
else
  TEST_DIR  := $(BUILD_DIR)
  TEST_C_SRCS := $(filter-out $(NATIVE_TESTS),$(wildcard tests/*.c))
  TEST_C_BINS := $(patsubst tests/%.c,$(BUILD_DIR)/%,$(TEST_C_SRCS))
  TEST_DEPS := $(BUILD_DIR)/test-hello $(TEST_C_BINS)
  TEST_HELLO_DEP := $(BUILD_DIR)/test-hello
endif

# Colors (used by test output)
GREEN  := \033[0;32m
BLUE   := \033[0;34m
CYAN   := \033[0;36m
YELLOW := \033[1;33m
RED    := \033[0;31m
RESET  := \033[0m

# Compiler flags
CFLAGS := -O2 -Wall -Wextra -Wpedantic \
          -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
          -Wformat=2 -Wimplicit-fallthrough -Wundef \
          -Wnull-dereference -Wno-unused-parameter
