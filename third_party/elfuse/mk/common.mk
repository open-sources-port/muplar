# mk/common.mk — Generic build rules
#
# Per-file compilation with automatic dependency tracking, verbosity
# control, and kernel-style build output.  Inspired by libiui's build
# infrastructure.

# Verbosity: make V=1 shows full commands
ifeq ($(V),1)
    Q :=
else
    Q := @
    MAKEFLAGS += --no-print-directory
endif

$(BUILD_DIR):
	@mkdir -p $@

# Automatic header dependency generation (-MMD -MP)
DEPFLAGS = -MMD -MP -MF $(BUILD_DIR)/$(subst /,_,$*).d

# Pattern rules — source to object.
# GENERATED_HEADERS are order-only prerequisites so clean builds have the
# build-generated includes available before compilation. .d files track the
# real header dependencies after the first compile. Generators whose output
# triggers a rebuild on input change (e.g., build/dispatch.h from
# src/syscall/dispatch.tbl) use explicit normal prerequisites where needed.

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR) $(GENERATED_HEADERS)
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -I$(BUILD_DIR) -Isrc -c -o $@ $<

$(BUILD_DIR)/%.o: tests/%.c | $(BUILD_DIR) $(GENERATED_HEADERS)
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -I$(BUILD_DIR) -Isrc -c -o $@ $<

# Include generated dependency files (silently skip on first build)
-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: clean distclean

## Remove all build artifacts
clean:
	rm -rf $(BUILD_DIR)

## Remove build artifacts plus downloaded test fixtures (Alpine packages, kernel, initramfs, busybox)
distclean: clean
	rm -rf externals/test-fixtures
