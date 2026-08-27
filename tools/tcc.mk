# Shared source list for vendored TCC (Tiny C Compiler), used by every seat
# that links it: host metal.mk, unix metal.mk, and firmware (fw_tcc.mk).
# One list — a seat that compiles the core has no second copy to drift.
#
# TCC_TARGET_X86_64 is the natural host target; the compiler is embedded
# as a library (libtcc API), not as a standalone binary.

ifndef PM_METAL_TCC_MK
PM_METAL_TCC_MK := 1

ifndef TCC_DIR
TCC_DIR ?= $(CURDIR)/externals/tcc
endif

TCC_CORE_SRCS := \
	libtcc.c \
	tccpp.c \
	tccgen.c \
	tccelf.c \
	tccasm.c \
	tccdbg.c \
	tccrun.c \
	tcctools.c \
	x86_64-gen.c \
	x86_64-link.c

endif