# jit/c/tccsrc_embed.inc.h — the vendored TCC entry source as embedded bytes.
#
# The jit.c card's self-host prove feeds this to its own object_compile:
# TCC compiling TCC, in-process, with the include dir pointing at the
# vendored tree so libtcc.c's ONE_SOURCE #includes resolve (config.h,
# tccpp.c, x86_64-gen.c, ... — the full translation set).
#
# source-in-its-lang: the muscle is externals/tcc/libtcc.c (a real file,
# never edited here); the generated array is not where you edit. Regenerated
# at parse time so the include exists before the first .o, like ledger.mk.

ifndef PM_METAL_TCCSRC_MK
PM_METAL_TCCSRC_MK := 1

PM_METAL_TCCSRC_TOOLS := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
PM_METAL_TCCSRC_ROOT := $(patsubst %/,%,$(dir $(PM_METAL_TCCSRC_TOOLS)))
PM_METAL_TCCSRC_INC := $(PM_METAL_TCCSRC_ROOT)/src/pymergetic/metal/jit/c/tccsrc_embed.inc.h
PM_METAL_TCCSRC_SRC := $(PM_METAL_TCCSRC_ROOT)/externals/tcc/libtcc.c

ifneq ($(MAKECMDGOALS),clean)
PM_METAL_TCCSRC_FAIL := $(shell python3 $(PM_METAL_TCCSRC_ROOT)/port/embed_bytes.py -o $(PM_METAL_TCCSRC_INC) --str pm_metal_jit_c_tcc_source $(PM_METAL_TCCSRC_SRC) || echo fail)
ifneq ($(PM_METAL_TCCSRC_FAIL),)
$(error metal jit/c tcc source embed failed — run port/embed_bytes.py)
endif
endif

endif
