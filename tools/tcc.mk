# Shared source list for vendored TCC (Tiny C Compiler), used by every seat
# that links it: host metal.mk, unix metal.mk, and firmware (fw_tcc.mk).
# One list — the externals/tcc/__pmm__.toml manifest, regenerable via
# `tools/externals.sh gen tcc`.
#
# TCC_TARGET_X86_64 is the natural host target; the compiler is embedded
# as a library (libtcc API), not as a standalone binary.

ifndef PM_METAL_TCC_MK
PM_METAL_TCC_MK := 1

ifndef TCC_DIR
TCC_DIR ?= $(CURDIR)/externals/tcc
endif

EXTERNALS_SH := $(dir $(lastword $(MAKEFILE_LIST)))externals.sh
TCC_MANIFEST_SRCS := $(shell $(EXTERNALS_SH) list tcc)

ifeq ($(TCC_MANIFEST_SRCS),)
$(error tools/tcc.mk: externals.sh list tcc returned nothing)
endif

# config.h defines CONFIG_TRIPLET only when no TCC_TARGET_* is predefined; every
# seat predefines one, so without this the embedded library searches /usr/lib
# (no libc.so on multiarch hosts) and tcc_relocate fails with "library 'c' not
# found". Derive the triplet from the host compiler, like tcc's configure does.
TCC_TRIPLET := $(shell $(CC) -print-multiarch 2>/dev/null)
ifneq ($(TCC_TRIPLET),)
TCC_DEFINES := -DCONFIG_TRIPLET=\"$(TCC_TRIPLET)\"
else
TCC_DEFINES :=
endif

endif
