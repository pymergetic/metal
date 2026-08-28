# build/changes_embed.inc.h — the build card's ledger seed as embedded bytes.
#
# The change ledger's initial lines are authored in a real .jsonl file beside
# the muscle (source-in-its-lang: the seed is a file, the generated array is
# not where you edit). Every seat that compiles the build card includes this,
# exactly like tools/src.mk; generation runs at parse time so the include
# exists before the first .o.

ifndef PM_METAL_LEDGER_MK
PM_METAL_LEDGER_MK := 1

PM_METAL_LEDGER_TOOLS := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
PM_METAL_LEDGER_ROOT := $(patsubst %/,%,$(dir $(PM_METAL_LEDGER_TOOLS)))
PM_METAL_LEDGER_INC := $(PM_METAL_LEDGER_ROOT)/src/pymergetic/metal/build/changes_embed.inc.h
PM_METAL_LEDGER_SRC := $(PM_METAL_LEDGER_ROOT)/src/pymergetic/metal/build/changes.jsonl

ifneq ($(MAKECMDGOALS),clean)
PM_METAL_LEDGER_FAIL := $(shell python3 $(PM_METAL_LEDGER_ROOT)/port/embed_bytes.py -o $(PM_METAL_LEDGER_INC) --str pm_metal_build_changes_jsonl $(PM_METAL_LEDGER_SRC) || echo fail)
ifneq ($(PM_METAL_LEDGER_FAIL),)
$(error metal ledger embed failed — run port/embed_bytes.py)
endif
endif

endif
