# Test targets

.PHONY: test-hello test-all check test-gdbstub test-coreutils test-busybox \
        test-static-bins \
        test-dynamic test-dynamic-coreutils test-glibc-dynamic \
        test-glibc-coreutils test-perf \
        test-matrix test-matrix-elfuse-aarch64 test-matrix-qemu-aarch64 \
        test-full test-multi-vcpu test-rwx test-sysroot-rename \
        test-sysroot-procfs-exec test-timeout-disable \
        test-sysroot-nofollow test-sysroot-chdir perf

## Build and run the assembly hello world test
test-hello: $(ELFUSE_BIN) $(TEST_HELLO_DEP)
	@printf "$(BLUE)▸ Running$(RESET) test-hello\n"
	$(ELFUSE_BIN) $(TEST_DIR)/test-hello

## Run the unit test suite plus busybox applet validation
check: $(ELFUSE_BIN) $(TEST_DEPS)
	@bash tests/driver.sh -e $(ELFUSE_BIN) -d $(TEST_DIR) -v
	@printf "\n$(BLUE)━━━ busybox applet validation ━━━$(RESET)\n"
	@$(MAKE) --no-print-directory test-busybox
	@printf "\n$(BLUE)━━━ sysroot procfs exec validation ━━━$(RESET)\n"
	@$(MAKE) --no-print-directory test-sysroot-procfs-exec
	@printf "\n$(BLUE)━━━ timeout=0 validation ━━━$(RESET)\n"
	@$(MAKE) --no-print-directory test-timeout-disable

test-sysroot-rename: $(ELFUSE_BIN) $(BUILD_DIR)/test-sysroot-rename
	@tmpdir=$$(mktemp -d); \
	trap 'rm -rf "$$tmpdir"; rm -f /tmp/elfuse-sysroot-rename-dst.txt' EXIT; \
	mkdir -p "$$tmpdir/tmp"; \
	printf 'inside-sysroot\n' > "$$tmpdir/tmp/elfuse-sysroot-rename-src.txt"; \
	rm -f /tmp/elfuse-sysroot-rename-dst.txt; \
	$(ELFUSE_BIN) --sysroot "$$tmpdir" $(BUILD_DIR)/test-sysroot-rename; \
	if [ ! -f "$$tmpdir/tmp/elfuse-sysroot-rename-dst.txt" ]; then \
		printf "$(RED)FAIL$(RESET) rename did not create destination in sysroot\n"; \
		exit 1; \
	fi; \
	if [ -e /tmp/elfuse-sysroot-rename-dst.txt ]; then \
		printf "$(RED)FAIL$(RESET) rename escaped sysroot to host /tmp\n"; \
		exit 1; \
	fi

test-sysroot-nofollow: $(ELFUSE_BIN) $(BUILD_DIR)/test-sysroot-nofollow
	@tmpdir=$$(mktemp -d); \
	trap 'rm -rf "$$tmpdir"' EXIT; \
	mkdir -p "$$tmpdir/tmp"; \
	ln -sf /outside-target "$$tmpdir/tmp/elfuse-sysroot-nofollow-link"; \
	$(ELFUSE_BIN) --sysroot "$$tmpdir" $(BUILD_DIR)/test-sysroot-nofollow

test-sysroot-chdir: $(ELFUSE_BIN) $(BUILD_DIR)/test-sysroot-chdir
	@tmpdir=$$(mktemp -d); \
	trap 'rm -rf "$$tmpdir"' EXIT; \
	mkdir -p "$$tmpdir/bin" "$$tmpdir/lib" "$$tmpdir/lib/elfuse-sysroot-shadow"; \
	$(ELFUSE_BIN) --sysroot "$$tmpdir" $(BUILD_DIR)/test-sysroot-chdir

test-sysroot-procfs-exec: $(ELFUSE_BIN) $(BUILD_DIR)/test-procfs-exec
	@tmpdir=$$(mktemp -d); \
	trap 'rm -rf "$$tmpdir"' EXIT; \
	mkdir -p "$$tmpdir/bin"; \
	cp $(BUILD_DIR)/test-procfs-exec "$$tmpdir/bin/test-procfs-exec"; \
	$(ELFUSE_BIN) --sysroot "$$tmpdir" "$$tmpdir/bin/test-procfs-exec"

test-timeout-disable: $(ELFUSE_BIN) $(TEST_HELLO_DEP)
	@$(ELFUSE_BIN) --timeout 0 $(TEST_DIR)/test-hello > /dev/null

## Run GDB stub integration tests (LLDB <-> elfuse gdbstub)
test-gdbstub: $(ELFUSE_BIN) $(TEST_DIR)/test-hello
	@bash tests/test-gdbstub.sh -e $(ELFUSE_BIN) -v

## Alias for check (backward compat)
test-all: check

# ── Coreutils integration test ───────────────────────────────────

FIXTURES_DIR ?= $(CURDIR)/externals/test-fixtures

ifeq ($(origin GUEST_COREUTILS), undefined)
  ifneq ($(wildcard $(FIXTURES_DIR)/aarch64-musl/dyn-bin),)
    GUEST_COREUTILS := $(FIXTURES_DIR)/aarch64-musl/dyn-bin
  endif
endif

ifeq ($(origin GUEST_BUSYBOX), undefined)
  ifneq ($(wildcard $(FIXTURES_DIR)/aarch64-musl/staticbin/bin/busybox),)
    GUEST_BUSYBOX := $(FIXTURES_DIR)/aarch64-musl/staticbin/bin/busybox
  endif
endif

ifeq ($(origin GUEST_STATIC_BINS), undefined)
  ifneq ($(wildcard $(FIXTURES_DIR)/aarch64-musl/dyn-bin),)
    GUEST_STATIC_BINS := $(FIXTURES_DIR)/aarch64-musl/dyn-bin
  endif
endif

ifeq ($(origin GUEST_SYSROOT), undefined)
  ifneq ($(wildcard $(FIXTURES_DIR)/rootfs),)
    GUEST_SYSROOT := $(FIXTURES_DIR)/rootfs
  endif
endif

ifeq ($(origin GUEST_DYNAMIC_COREUTILS), undefined)
  ifneq ($(wildcard $(FIXTURES_DIR)/aarch64-musl/dyn-bin),)
    GUEST_DYNAMIC_COREUTILS := $(FIXTURES_DIR)/aarch64-musl/dyn-bin
  endif
endif

# Path to static aarch64-linux coreutils bin directory.
# Auto-detected from GUEST_COREUTILS; override with COREUTILS_BIN=...
ifdef GUEST_COREUTILS
  ifneq ($(wildcard $(GUEST_COREUTILS)/bin),)
    COREUTILS_BIN ?= $(GUEST_COREUTILS)/bin
  else
    COREUTILS_BIN ?= $(GUEST_COREUTILS)
  endif
endif

## Run GNU coreutils 9.9 integration tests (104 tools)
test-coreutils: $(ELFUSE_BIN)
	@if [ ! -d "$(COREUTILS_BIN)" ]; then \
		printf "$(RED)✗ Coreutils not found.$(RESET) Set COREUTILS_BIN=/path/to/bin.\n"; \
		exit 1; \
	fi
	@if [ "$(COREUTILS_BIN)" = "$(FIXTURES_DIR)/aarch64-musl/dyn-bin" ]; then \
		bash tests/test-coreutils-smoke.sh $(ELFUSE_BIN) $(COREUTILS_BIN) $(SYSROOT_DIR); \
	elif [ -n "$(SYSROOT_DIR)" ] && [ -d "$(SYSROOT_DIR)" ]; then \
		bash tests/test-coreutils.sh $(ELFUSE_BIN) $(COREUTILS_BIN) $(SYSROOT_DIR); \
	else \
		bash tests/test-coreutils.sh $(ELFUSE_BIN) $(COREUTILS_BIN); \
	fi

# ── Busybox integration test ─────────────────────────────────────

ifneq ($(wildcard $(BUILD_DIR)/busybox),)
  BUSYBOX_BIN ?= $(BUILD_DIR)/busybox
else ifdef GUEST_BUSYBOX
  ifneq ($(wildcard $(GUEST_BUSYBOX)/bin/busybox),)
    BUSYBOX_BIN ?= $(GUEST_BUSYBOX)/bin/busybox
  else
    BUSYBOX_BIN ?= $(GUEST_BUSYBOX)
  endif
else
  BUSYBOX_BIN ?= $(BUILD_DIR)/busybox
endif

BUSYBOX_SUITE ?= sid
BUSYBOX_PACKAGE_PAGE ?= https://packages.debian.org/$(BUSYBOX_SUITE)/busybox-static
BUSYBOX_DOWNLOAD_PAGE ?= https://packages.debian.org/$(BUSYBOX_SUITE)/arm64/busybox-static/download

ifeq ($(BUSYBOX_BIN),$(BUILD_DIR)/busybox)
  BUSYBOX_DEPS := $(BUILD_DIR)/busybox
else
  BUSYBOX_DEPS :=
endif

$(BUILD_DIR)/busybox: | $(BUILD_DIR)
	@printf "$(BLUE)▸ Downloading$(RESET) busybox-static (arm64) from $(BUSYBOX_PACKAGE_PAGE)\n"
	@tmpdir="$(BUILD_DIR)/busybox-static.tmp"; \
	rm -rf "$$tmpdir"; \
	mkdir -p "$$tmpdir"; \
	package_page="$$tmpdir/package.html"; \
	download_page="$$tmpdir/download.html"; \
	deb="$$tmpdir/busybox-static.deb"; \
	curl -fsSL "$(BUSYBOX_PACKAGE_PAGE)" -o "$$package_page"; \
	download_url=$$(sed -n 's/.*href="\([^"]*\/arm64\/busybox-static\/download\)".*/\1/p' "$$package_page" | head -n 1); \
	if [ -z "$$download_url" ]; then \
		download_url="$(BUSYBOX_DOWNLOAD_PAGE)"; \
	fi; \
	case "$$download_url" in \
		http*) ;; \
		*) download_url="https://packages.debian.org$$download_url" ;; \
	esac; \
	curl -fsSL "$$download_url" -o "$$download_page"; \
	deb_url=$$(sed -n 's/.*href="\([^"]*busybox-static_[^"]*_arm64\.deb\)".*/\1/p' "$$download_page" | head -n 1); \
	if [ -z "$$deb_url" ]; then \
		printf "$(RED)✗ Could not find busybox-static .deb link on %s$(RESET)\n" "$$download_url"; \
		exit 1; \
	fi; \
	case "$$deb_url" in \
		http*) ;; \
		//*) deb_url="https:$$deb_url" ;; \
		*) deb_url="https://packages.debian.org$$deb_url" ;; \
	esac; \
	curl -fL "$$deb_url" -o "$$deb"; \
	( cd "$$tmpdir" && ar x busybox-static.deb ); \
	data_archive=$$(find "$$tmpdir" -maxdepth 1 -name 'data.tar.*' -print | head -n 1); \
	if [ -z "$$data_archive" ]; then \
		printf "$(RED)✗ Debian package did not contain data.tar.*$(RESET)\n"; \
		exit 1; \
	fi; \
	mkdir -p "$$tmpdir/root"; \
	tar -xf "$$data_archive" -C "$$tmpdir/root"; \
	if [ ! -x "$$tmpdir/root/usr/bin/busybox" ]; then \
		printf "$(RED)✗ Debian package did not contain /usr/bin/busybox$(RESET)\n"; \
		exit 1; \
	fi; \
	cp "$$tmpdir/root/usr/bin/busybox" "$@"; \
	chmod 0755 "$@"; \
	rm -rf "$$tmpdir"

## Run busybox applet smoke tests
test-busybox: $(ELFUSE_BIN) $(BUSYBOX_DEPS)
	@if [ ! -x "$(BUSYBOX_BIN)" ]; then \
		printf "$(RED)✗ Busybox not found.$(RESET) Set BUSYBOX_BIN=/path/to/busybox.\n"; \
		exit 1; \
	fi
	@bash tests/test-busybox.sh $(ELFUSE_BIN) $(BUSYBOX_BIN)

# ── Static binary integration tests ──────────────────────────────

ifdef GUEST_STATIC_BINS
  ifneq ($(wildcard $(GUEST_STATIC_BINS)/bin),)
    STATIC_BINS_DIR ?= $(GUEST_STATIC_BINS)/bin
  else
    STATIC_BINS_DIR ?= $(GUEST_STATIC_BINS)
  endif
endif

## Run static binary smoke tests (bash, lua, gawk, jq, sqlite, etc.)
test-static-bins: $(ELFUSE_BIN)
	@if [ ! -d "$(STATIC_BINS_DIR)" ]; then \
		printf "$(RED)✗ Static bins not found.$(RESET) Set STATIC_BINS_DIR=/path/to/bin.\n"; \
		exit 1; \
	fi
	@if [ -n "$(SYSROOT_DIR)" ] && [ -d "$(SYSROOT_DIR)" ]; then \
		bash tests/test-static-bins.sh $(ELFUSE_BIN) $(STATIC_BINS_DIR) $(SYSROOT_DIR); \
	else \
		bash tests/test-static-bins.sh $(ELFUSE_BIN) $(STATIC_BINS_DIR); \
	fi

# ── Dynamic linking tests ────────────────────────────────────────

# Musl sysroot with dynamic linker + libc.so.
SYSROOT_DIR ?= $(GUEST_SYSROOT)
ifdef GUEST_DYNAMIC_COREUTILS
  ifneq ($(wildcard $(GUEST_DYNAMIC_COREUTILS)/bin),)
    DYNAMIC_COREUTILS_BIN ?= $(GUEST_DYNAMIC_COREUTILS)/bin
  else
    DYNAMIC_COREUTILS_BIN ?= $(GUEST_DYNAMIC_COREUTILS)
  endif
endif

## Run dynamic linking smoke test (hello-dynamic via --sysroot)
test-dynamic: $(ELFUSE_BIN)
	@if [ -z "$(SYSROOT_DIR)" ] || [ ! -d "$(SYSROOT_DIR)" ]; then \
		printf "$(RED)✗ Sysroot not found.$(RESET) Set SYSROOT_DIR=/path/to/sysroot.\n"; \
		exit 1; \
	fi
	@printf "$(BLUE)▸ Running$(RESET) dynamic hello-dynamic (--sysroot)\n"
	$(ELFUSE_BIN) --sysroot $(SYSROOT_DIR) $(GUEST_DYNAMIC_TESTS)/bin/hello-dynamic

## Run dynamically-linked coreutils tests (--sysroot)
test-dynamic-coreutils: $(ELFUSE_BIN)
	@if [ -z "$(SYSROOT_DIR)" ] || [ ! -d "$(SYSROOT_DIR)" ]; then \
		printf "$(RED)✗ Sysroot not found.$(RESET) Set SYSROOT_DIR=/path/to/sysroot.\n"; \
		exit 1; \
	fi
	@if [ ! -d "$(DYNAMIC_COREUTILS_BIN)" ]; then \
		printf "$(RED)✗ Dynamic coreutils not found.$(RESET) Set DYNAMIC_COREUTILS_BIN=/path/to/bin.\n"; \
		exit 1; \
	fi
	@if [ "$(DYNAMIC_COREUTILS_BIN)" = "$(FIXTURES_DIR)/aarch64-musl/dyn-bin" ]; then \
		bash tests/test-coreutils-smoke.sh $(ELFUSE_BIN) $(DYNAMIC_COREUTILS_BIN) $(SYSROOT_DIR); \
	else \
		bash tests/test-dynamic-coreutils.sh $(ELFUSE_BIN) $(SYSROOT_DIR) $(DYNAMIC_COREUTILS_BIN); \
	fi

# ── glibc dynamic linking tests ───────────────────────────────────

# glibc sysroot with dynamic linker + libc.so.
GLIBC_SYSROOT_DIR ?= $(GUEST_GLIBC_SYSROOT)
ifdef GUEST_GLIBC_DYNAMIC_COREUTILS
  ifneq ($(wildcard $(GUEST_GLIBC_DYNAMIC_COREUTILS)/bin),)
    GLIBC_DYNAMIC_COREUTILS_BIN ?= $(GUEST_GLIBC_DYNAMIC_COREUTILS)/bin
  else
    GLIBC_DYNAMIC_COREUTILS_BIN ?= $(GUEST_GLIBC_DYNAMIC_COREUTILS)
  endif
endif

## Run glibc dynamic linking smoke test (hello-dynamic via --sysroot)
test-glibc-dynamic: $(ELFUSE_BIN)
	@if [ -z "$(GLIBC_SYSROOT_DIR)" ] || [ ! -d "$(GLIBC_SYSROOT_DIR)" ]; then \
		printf "$(RED)✗ glibc sysroot not found.$(RESET) Set GLIBC_SYSROOT_DIR=/path/to/sysroot.\n"; \
		exit 1; \
	fi
	@printf "$(BLUE)▸ Running$(RESET) glibc hello-dynamic (--sysroot)\n"
	$(ELFUSE_BIN) --sysroot $(GLIBC_SYSROOT_DIR) $(GUEST_GLIBC_DYNAMIC_TESTS)/bin/hello-dynamic

## Run glibc dynamically-linked coreutils tests (--sysroot)
test-glibc-coreutils: $(ELFUSE_BIN)
	@if [ -z "$(GLIBC_SYSROOT_DIR)" ] || [ ! -d "$(GLIBC_SYSROOT_DIR)" ]; then \
		printf "$(RED)✗ glibc sysroot not found.$(RESET) Set GLIBC_SYSROOT_DIR=/path/to/sysroot.\n"; \
		exit 1; \
	fi
	@if [ ! -d "$(GLIBC_DYNAMIC_COREUTILS_BIN)" ]; then \
		printf "$(RED)✗ glibc dynamic coreutils not found.$(RESET) Set GLIBC_DYNAMIC_COREUTILS_BIN=/path/to/bin.\n"; \
		exit 1; \
	fi
	@SUITE_LABEL="glibc dynamic GNU coreutils test suite (--sysroot)" \
	    SUITE_SUMMARY="glibc results" \
	    bash tests/test-dynamic-coreutils.sh $(ELFUSE_BIN) $(GLIBC_SYSROOT_DIR) $(GLIBC_DYNAMIC_COREUTILS_BIN)

# ── Performance benchmark ─────────────────────────────────────────

ifneq ($(wildcard $(BUILD_DIR)/busybox),)
  PERF_BIN ?= $(BUILD_DIR)/perf-bin
  PERF_DEPS := $(addprefix $(PERF_BIN)/,grep wc cat sort)
else
  PERF_BIN ?= $(COREUTILS_BIN)
  PERF_DEPS :=
endif

$(BUILD_DIR)/perf-bin:
	@mkdir -p $@

$(BUILD_DIR)/perf-bin/%: $(BUILD_DIR)/busybox | $(BUILD_DIR)/perf-bin
	@ln -sf ../busybox $@

## Run performance benchmarks (native vs elfuse, 10 iterations each)
test-perf: $(ELFUSE_BIN) $(PERF_DEPS)
	@if [ ! -d "$(PERF_BIN)" ]; then \
		printf "$(RED)✗ Perf tools not found.$(RESET) Set COREUTILS_BIN=/path/to/bin or provide $(BUILD_DIR)/busybox.\n"; \
		exit 1; \
	fi
	@bash tests/test-perf.sh $(ELFUSE_BIN) $(PERF_BIN)

## Alias for test-perf
perf: test-perf

# ── Test matrix (elfuse + qemu, aarch64) ────────────────────────────────

## Run full test matrix (all modes: elfuse + qemu, aarch64)
test-matrix: $(ELFUSE_BIN) $(TEST_DEPS)
	@bash tests/test-matrix.sh all

## Run test matrix: elfuse aarch64 mode
test-matrix-elfuse-aarch64: $(ELFUSE_BIN) $(TEST_DEPS)
	@bash tests/test-matrix.sh elfuse-aarch64

## Run test matrix: qemu aarch64 mode
test-matrix-qemu-aarch64: $(ELFUSE_BIN) $(TEST_DEPS)
	@bash tests/test-matrix.sh qemu-aarch64

# ── Full test suite ──────────────────────────────────────────────────

## Run the complete test suite (aarch64: unit + busybox + gdbstub + coreutils + static + dynamic)
test-full: $(ELFUSE_BIN)
	@printf "\n$(CYAN)╔══════════════════════════════════════════════════════╗$(RESET)\n"
	@printf "$(CYAN)║              elfuse full test suite                      ║$(RESET)\n"
	@printf "$(CYAN)╚══════════════════════════════════════════════════════╝$(RESET)\n"
	@fail=0; \
	printf "\n$(BLUE)━━━ [1/6] aarch64 unit tests + busybox ━━━$(RESET)\n"; \
	$(MAKE) --no-print-directory check || fail=$$((fail + 1)); \
	printf "\n$(BLUE)━━━ [2/6] GDB stub integration (LLDB) ━━━$(RESET)\n"; \
	$(MAKE) --no-print-directory test-gdbstub || fail=$$((fail + 1)); \
	printf "\n$(BLUE)━━━ [3/6] aarch64 coreutils (static) ━━━$(RESET)\n"; \
	if [ -n "$(COREUTILS_BIN)" ] && [ -d "$(COREUTILS_BIN)" ] && \
	   [ "$(COREUTILS_BIN)" != "$(FIXTURES_DIR)/aarch64-musl/dyn-bin" ]; then \
		$(MAKE) --no-print-directory test-coreutils || fail=$$((fail + 1)); \
	else \
		printf "$(YELLOW)SKIP$(RESET) static coreutils suite (set COREUTILS_BIN to a dedicated full coreutils bundle)\n"; \
	fi; \
	printf "\n$(BLUE)━━━ [4/6] aarch64 static bins (bash, jq, sqlite, lua, ...) ━━━$(RESET)\n"; \
	$(MAKE) --no-print-directory test-static-bins || fail=$$((fail + 1)); \
	printf "\n$(BLUE)━━━ [5/6] aarch64 dynamic coreutils (musl) ━━━$(RESET)\n"; \
	$(MAKE) --no-print-directory test-dynamic-coreutils || fail=$$((fail + 1)); \
	printf "\n$(BLUE)━━━ [6/6] aarch64 dynamic coreutils (glibc) ━━━$(RESET)\n"; \
	if [ -n "$(GLIBC_SYSROOT_DIR)" ] && [ -d "$(GLIBC_SYSROOT_DIR)" ] && \
	   [ -n "$(GLIBC_DYNAMIC_COREUTILS_BIN)" ] && [ -d "$(GLIBC_DYNAMIC_COREUTILS_BIN)" ]; then \
		$(MAKE) --no-print-directory test-glibc-coreutils || fail=$$((fail + 1)); \
	else \
		printf "$(YELLOW)SKIP$(RESET) glibc dynamic coreutils (set GLIBC_SYSROOT_DIR and GLIBC_DYNAMIC_COREUTILS_BIN)\n"; \
	fi; \
	printf "\n$(CYAN)╔══════════════════════════════════════════════════════╗$(RESET)\n"; \
	if [ "$$fail" -eq 0 ]; then \
		printf "$(CYAN)║  $(GREEN)✓ All required suites passed$(CYAN)                      ║$(RESET)\n"; \
	else \
		printf "$(CYAN)║  $(RED)✗ $$fail suite(s) had failures$(CYAN)                        ║$(RESET)\n"; \
	fi; \
	printf "$(CYAN)╚══════════════════════════════════════════════════════╝$(RESET)\n"; \
	[ "$$fail" -eq 0 ]

# ── Multi-vCPU validation test ─────────────────────────────────────
# Build rules in top-level Makefile; these are just run targets.

## Run multi-vCPU validation tests (5 tests)
test-multi-vcpu: $(BUILD_DIR)/test-multi-vcpu
	$(BUILD_DIR)/test-multi-vcpu

# ── RWX page table entry test ───────────────────────────────────

## Run RWX page table entry test (does HVF allow W+X?)
test-rwx: $(BUILD_DIR)/test-rwx
	$(BUILD_DIR)/test-rwx
