# Matrix ledger + browser import smoke.
METAL := $(abspath ../..)
.PHONY: ledger browser all
ledger:
	python3 $(CURDIR)/test_module_matrix_ledger.py
browser:
	$(CURDIR)/run_browser_smoke.sh
all: ledger browser
