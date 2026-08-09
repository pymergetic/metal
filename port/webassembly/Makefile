# Browser metal seat (CDN engine `mp`): wasmmod + frozen arch.wasm.
#
#   make -C extmod/metal/port/webassembly
#   → $(TOP)/ports/webassembly/build-metal/micropython.{mjs,wasm}

METAL_WEBASSEMBLY_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
METAL_DIR := $(abspath $(METAL_WEBASSEMBLY_DIR)/../..)
TOP ?= $(abspath $(METAL_DIR)/../..)
WASMMOD_DIR := $(TOP)/extmod/wasmmod
WEBASSEMBLY_PORT := $(TOP)/ports/webassembly
VARIANT_DIR := $(METAL_WEBASSEMBLY_DIR)/variant
BUILD ?= $(WEBASSEMBLY_PORT)/build-metal

SIGN_ROOT ?= $(WASMMOD_DIR)/examples/.keys/trust/root.crt.der
MICROPY_WASM_VERIFY ?= 1
MICROPY_WASM_TRUST_CA ?= $(SIGN_ROOT)
WASMMOD_EMBED_HOST_SOURCE ?= 1
HOST_ALIAS := $(BUILD)/pymergetic.wasmmod.wasm
# wasmmod tools (trust CA bake + embed-host)
export PYTHONPATH := $(WASMMOD_DIR)/dev/tools/src$(if $(PYTHONPATH),:$(PYTHONPATH))

.PHONY: all clean ensure-trust embed-host-desc
.DEFAULT_GOAL := all

ensure-trust:
	@if [ "$(MICROPY_WASM_VERIFY)" != "0" ] && [ -n "$(strip $(MICROPY_WASM_TRUST_CA))" ]; then \
		if [ ! -f "$(MICROPY_WASM_TRUST_CA)" ]; then \
			echo "==> gen demo PKI for TRUST_CA ($(MICROPY_WASM_TRUST_CA))"; \
			$(MAKE) -C $(WASMMOD_DIR)/examples sign-key; \
		fi; \
		test -f "$(MICROPY_WASM_TRUST_CA)" || { echo "FAIL: missing $(MICROPY_WASM_TRUST_CA)"; exit 1; }; \
	fi

all: ensure-trust
	$(MAKE) -C $(WEBASSEMBLY_PORT) \
		VARIANT_DIR=$(VARIANT_DIR) \
		BUILD=$(BUILD) \
		METAL=$(METAL_DIR) \
		MICROPY_MANIFEST_METAL=$(METAL_DIR) \
		MICROPY_WASM_VERIFY=$(MICROPY_WASM_VERIFY) \
		MICROPY_WASM_TRUST_CA="$(MICROPY_WASM_TRUST_CA)" \
		PYTHONPATH="$(PYTHONPATH)" \
		-j$(shell nproc 2>/dev/null || echo 4)
	@$(MAKE) embed-host-desc

embed-host-desc:
ifeq ($(WASMMOD_EMBED_HOST_SOURCE),1)
	@test -f "$(BUILD)/micropython.wasm" || { echo "FAIL: missing $(BUILD)/micropython.wasm"; exit 1; }
	@echo "==> embed host self-desc (pymergetic.wasmmod) into micropython.wasm"
	PYTHONPATH="$(PYTHONPATH)" python3 -m pymergetic.wasmmod.tools embed-host "$(BUILD)/micropython.wasm" \
		--wasmmod-root "$(WASMMOD_DIR)" \
		--alias "$(HOST_ALIAS)"
else
	@echo "==> WASMMOD_EMBED_HOST_SOURCE=0 — skip host source embed"
endif

clean:
	$(MAKE) -C $(WEBASSEMBLY_PORT) VARIANT_DIR=$(VARIANT_DIR) BUILD=$(BUILD) clean
	rm -f "$(HOST_ALIAS)"
