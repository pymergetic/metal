# inspect/src_embed.inc.h — each card's authored muscle source as bytes
# (.c/.rs muscle for C/RS cards, .py files for PY cards — the build card
# compiles the py muscle to mpy bytecode in-kernel).
#
# Include from every seat that compiles inspect (host Makefile, metal.mk,
# fw_cards.mk), exactly like tools/www.mk. Generation runs at parse time so the
# include exists before the first .o; embed_src.py leaves the file untouched
# when the cards' source bytes match.
#
# Seats must agree on the card roots so the embedded tree covers the same card
# namespace the registry reports: the metal cards (src/pymergetic/metal) plus
# the wasmmod cards (../wasmmod/src/pymergetic -> pymergetic.*).

ifndef PM_METAL_SRC_MK
PM_METAL_SRC_MK := 1

PM_METAL_SRC_TOOLS := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
PM_METAL_SRC_ROOT := $(patsubst %/,%,$(dir $(PM_METAL_SRC_TOOLS)))
PM_METAL_SRC_INC := $(PM_METAL_SRC_ROOT)/src/pymergetic/metal/inspect/src_embed.inc.h
PM_METAL_SRC_METAL := $(PM_METAL_SRC_ROOT)/src/pymergetic/metal
PM_METAL_SRC_WASMMOD := $(PM_METAL_SRC_ROOT)/../wasmmod/src/pymergetic

ifneq ($(MAKECMDGOALS),clean)
PM_METAL_SRC_FAIL := $(shell python3 $(PM_METAL_SRC_TOOLS)/embed_src.py -o $(PM_METAL_SRC_INC) $(PM_METAL_SRC_METAL) $(PM_METAL_SRC_WASMMOD) || echo fail)
ifneq ($(PM_METAL_SRC_FAIL),)
$(error metal source embed failed — run $(PM_METAL_SRC_TOOLS)/embed_src.py)
endif
endif

endif
