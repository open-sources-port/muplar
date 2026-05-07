# Help

.PHONY: help

## Display this help message
help:
	@printf "$(BLUE)elfuse — aarch64-linux ELF executor on macOS Apple Silicon$(RESET)\n\n"
	@printf "$(GREEN)Usage:$(RESET) make <target> [SIGN_IDENTITY=\"...\"]\n\n"
	@printf "$(GREEN)Targets:$(RESET)\n"
	@awk '/^[a-zA-Z\-\_0-9%:\\]+:/ { \
		helpMessage = match(lastLine, /^## (.*)/); \
		if (helpMessage) { \
			helpCommand = $$1; sub(/:$$/, "", helpCommand); \
			helpMessage = substr(lastLine, RSTART + 3, RLENGTH); \
			printf "  $(YELLOW)%-20s$(RESET) %s\n", helpCommand, helpMessage; \
		} \
	} \
	{ lastLine = $$0 }' $(MAKEFILE_LIST)
ifdef GUEST_TEST_BINARIES
	@printf "\n$(GREEN)Test binaries:$(RESET) pre-built ($(TEST_DIR))\n"
else
	@printf "\n$(GREEN)ELF toolchain:$(RESET) BAREMETAL_CROSS=$(BAREMETAL_CROSS)\n"
	@printf "$(GREEN)Cross-compiler:$(RESET) CROSS_COMPILE=$(CROSS_COMPILE)\n"
	@printf "  Override assembly tests with: make test-hello BAREMETAL_CROSS=aarch64-none-elf-\n"
	@printf "  Override C tests with:        make test-all CROSS_COMPILE=aarch64-linux-gnu-\n"
endif
