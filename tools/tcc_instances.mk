# tcc_instances.mk — one TCC recipe, N instances.
#
# A TCC instance is one libtcc.c ONE_SOURCE compile for one backend, plus
# (for cross instances) a symbol-prefix pass so several of them can link
# into the same binary. Cross instances rename EVERY defined global
# (tools/tcc_prefix_syms.sh — the old tcc_*/wasm_ grep leaked gen_negf,
# which both wasm32 and arm backends define; two cross instances in one
# binary would collide on it).
#
# Declare instances with one call each (after including this file):
#
#   $(eval $(call tcc_instance,x86_64,TCC_TARGET_X86_64,build/tcc/libtcc.o,))
#   $(eval $(call tcc_instance,wasm32_cross,TCC_TARGET_WASM32,build/tcc/libtcc_wasm_cross.o,pm_tccw_))
#   $(eval $(call tcc_instance,arm_eabi_cross,TCC_TARGET_ARM,build/tcc/libtcc_arm_cross.o,pm_tcca_))
#
# Arguments: 1=name  2=TCC backend define  3=object path  4=symbol prefix
#            (empty prefix = native instance, no rename pass)
#
# Per-seat instance LISTS differ (browser: wasm32 native only; unix: x86_64
# native + both crosses; firmware: per-board native) but the rule exists
# once, here. The prefix pipeline honors $(TCC_NM)/$(TCC_OBJCOPY) (default
# the plain tools) so a seat that cross-builds can override them.
#
# TCC_DEFINES (from tcc.mk) feeds every instance — config.h derives the
# triplet only when no TCC_TARGET_* is predefined and every seat predefines
# one, so without this the embedded library would search /usr/lib.
# LDOUBLE_SIZE differs per backend, so per-instance ONE_SOURCE compiles stay
# separate objects (they already are separate objects by construction).
#
# Companions:
#   tools/tcc.mk             — the shared source manifest + TCC_DEFINES
#   tools/tcc_prefix_syms.sh — the nm → objcopy rename pass (one place)

ifndef PM_METAL_TCC_INSTANCES_MK
PM_METAL_TCC_INSTANCES_MK := 1

PM_METAL_TCC_TOOLS_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
TCC_PREFIX_SYMS := $(PM_METAL_TCC_TOOLS_DIR)tcc_prefix_syms.sh
TCC_NM ?= nm
TCC_OBJCOPY ?= objcopy
# Toolchain seams for the recipe's libtcc.c compile. Hosted seats leave them
# empty (host clang, default target, system include resolution); a seat whose
# compiler needs an explicit triple/object format (UEFI: --target=x86_64-
# unknown-windows for COFF, plus its own fwinc-first include order) sets
# these instead of a diverging rule.
TCC_INC ?=
TCC_CFLAGS ?=

# $(call tcc_instance,name,backend_define,object,prefix[,extra_defines])
#
# Native instance (empty prefix): plain compile to $@.
# Cross instance: compile to $@.raw, then tcc_prefix_syms.sh renames every
# defined global to <prefix><sym> into $@ and removes the intermediates.
# The rename line expands to ":" for native instances (no prefix pass).
#
# extra_defines (5th arg, optional, empty on the hosted seats' 4-arg calls)
# carries per-seat config knobs: the firmware seats pass their freestanding
# set (CONFIG_TCC_STATIC/SEMLOCK=0/BACKTRACE=0/BCHECK=0) so libtcc.o drops
# the POSIX machinery fwinc has no answer for. Appended after TCC_DEFINES.
define tcc_instance
$(3): $$(TCC_DEPS)
	mkdir -p $$(dir $$@)
	$$(CC) -std=gnu11 -O1 -g -w $(TCC_CFLAGS) $(TCC_INC) -I$$(TCC_DIR) -D$(2) $$(TCC_DEFINES) $(5) -c -o $$@.raw $$(TCC_DIR)/libtcc.c
	$(if $(4),$$(TCC_PREFIX_SYMS) $(4) $$@.raw $$@ $$(TCC_NM) $$(TCC_OBJCOPY),mv -f $$@.raw $$@)
endef

endif
