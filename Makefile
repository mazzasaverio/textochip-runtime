# Entry points for agents and contributors. The real build lives in host/Makefile
# (PC build and tests) and in zephyr/ (board builds with west, see README.md).

HOST_TESTS = test-idle test-dist test-ai-vm test-ai-move test-color test-color-move \
             test-color-service test-ai

.PHONY: check check-fast dev

# The secret-free host suite, the same steps as .github/workflows/ci.yml.
check:
	$(MAKE) -C host textochip_vm_host
	@for t in $(HOST_TESTS); do $(MAKE) -s -C host $$t || exit 1; done
	git diff --check

# After every edit: the host demo links the whole runtime.
check-fast:
	$(MAKE) -s -C host textochip_vm_host
	git diff --check

# The host demo: the IDE's bytecode on the PC, no board.
dev:
	$(MAKE) -C host run
