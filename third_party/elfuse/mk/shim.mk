# EL1 kernel shim assembly pipeline
#
# shim.S -> shim.o -> shim.bin -> shim_blob.h (C byte array)

$(BUILD_DIR)/shim.o: src/core/shim.S | $(BUILD_DIR)
	@echo "  AS      $<"
	$(Q)$(SHIM_AS) $(SHIM_ASFLAGS) -o $@ $<

$(BUILD_DIR)/shim.bin: $(BUILD_DIR)/shim.o
	@echo "  OBJCOPY $@"
	$(Q)$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/shim_blob.h: $(BUILD_DIR)/shim.bin
	@echo "  GEN     $@"
	$(Q)tmp="$@.$$$$.tmp"; \
	xxd -i $< | \
		sed -e 's/unsigned char .*\[\]/static const unsigned char shim_bin[]/' \
		    -e 's/unsigned int .*_len/static const unsigned int shim_bin_len/' > "$$tmp"; \
	cmp -s "$$tmp" "$@" 2>/dev/null || mv "$$tmp" "$@"; \
	rm -f "$$tmp"

# Version header — regenerates when HEAD or index changes.
# cmp trick avoids unnecessary rebuilds when version string is unchanged.
VERSION_DEPS := $(wildcard .git/HEAD .git/index) mk/config.mk
$(BUILD_DIR)/version.h: $(VERSION_DEPS) | $(BUILD_DIR)
	$(Q)mkdir -p $(dir $@)
	$(Q)tmp="$@.$$$$.tmp"; \
	printf '#define ELFUSE_VERSION "%s"\n' "$(VERSION)" > "$$tmp"; \
	cmp -s "$$tmp" "$@" 2>/dev/null || mv "$$tmp" "$@"; \
	rm -f "$$tmp"
