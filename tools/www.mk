# inspect/www_embed.inc.h — bytes from www/, not a second copy of the HTML.
#
# Include from every seat that compiles inspect (host Makefile, metal.mk,
# fw_cards.mk). Generation runs at parse time so the include exists before
# the first .o. embed_www.py leaves the file untouched when the bytes match.

ifndef PM_METAL_WWW_MK
PM_METAL_WWW_MK := 1

PM_METAL_WWW_TOOLS := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
PM_METAL_WWW_ROOT := $(patsubst %/,%,$(dir $(PM_METAL_WWW_TOOLS)))
PM_METAL_WWW_INC := $(PM_METAL_WWW_ROOT)/src/pymergetic/metal/inspect/www_embed.inc.h
PM_METAL_WWW_DIR := $(PM_METAL_WWW_ROOT)/src/pymergetic/metal/inspect/www

PM_METAL_CARD_ROOT := $(PM_METAL_WWW_ROOT)/src/pymergetic/metal
# Card FQN ledger for the rendered home (every __pmm__.toml dir under the
# metal tree, as a dotted pymergetic.metal.* name). Drives the build-time
# CDN-catalog view at "/"; per-module detail is the live /inspect/reg RPC.
PM_METAL_HOME_FQNS := $(shell find $(PM_METAL_CARD_ROOT) -name __pmm__.toml | sed "s#$(PM_METAL_CARD_ROOT)/##; s#/__pmm__.toml##; s#/#.#g" | sed 's/^/pymergetic.metal./' | tr '\n' ' ')

ifneq ($(MAKECMDGOALS),clean)
# render_index imports the seat catalog driver (pymergetic.metal.*) to build
# the CDN-catalog "/" page, so the metal + wasmmod source trees must be on the
# embed's module path (namespace packages, no __init__.py on the host).
PM_METAL_EMBED_PYTHONPATH := $(PM_METAL_WWW_ROOT)/src:$(WASMMOD_SRC)
PM_METAL_WWW_FAIL := $(shell PYTHONPATH="$(PM_METAL_EMBED_PYTHONPATH)" python3 $(PM_METAL_WWW_TOOLS)/embed_www.py -o $(PM_METAL_WWW_INC) $(PM_METAL_WWW_DIR) --render-index "$(PM_METAL_HOME_FQNS)" || echo fail)
ifneq ($(PM_METAL_WWW_FAIL),)
$(error inspect www embed failed — run $(PM_METAL_WWW_TOOLS)/embed_www.py)
endif
endif

endif
