# Vendored TCC (externals/tcc) on the firmware seats — the in-kernel compile
# face (jit.c object path: every seat builds objects for its native arch;
# x64 boards native x86_64, the armv7 board native ARM, see the every-seat-
# builds-every-arch matrix in the plan).
#
# Include from a board build.mk AFTER CFLAGS_METAL/INC exist and BEFORE
# fw_cards.mk (jit.c must compile with PM_HAS_TCC=1 and the LIB_DIR).
#
# One TCC instance = one libtcc.c ONE_SOURCE compile via the shared recipe
# (tools/tcc_instances.mk) — native only on firmware (cross instances are
# the hosted seats' lane; no firmware seat links a second backend yet).
# FW_TCC_TARGET selects the backend define (default x86_64; ARMV7_RV1106
# sets the ARM triplet) and FW_TCC_LIB_DIR feeds jit.c's
# tcc_set_lib_path/tcc_add_library_path so TCC never searches /usr/lib.
#
# Freestanding config (verified by compiling tccrun.c/libtcc.c against
# fwinc — see the full probe in the phase-2 work):
#   CONFIG_TCC_STATIC=1    — dlopen/dlsym become link-time stubs, no dlfcn
#   CONFIG_TCC_SEMLOCK=0   — no POSIX semaphores (threads don't exist here)
#   CONFIG_TCC_BACKTRACE=0 — drops the signal-handler machinery entirely
#                            (sigaction/siginfo_t/ucontext vanish from tccrun)
#   CONFIG_TCC_BCHECK=0    — no bounds-checking runtime

TCC_DIR ?= $(METAL_DIR)/externals/tcc
FW_TCC_TARGET ?= x86_64
ifeq ($(FW_TCC_TARGET),x86_64)
FW_TCC_TARGET_DEFINE := TCC_TARGET_X86_64
else ifeq ($(FW_TCC_TARGET),arm)
FW_TCC_TARGET_DEFINE := TCC_TARGET_ARM
endif

FW_TCC_LIB_DIR := $(TCC_DIR)

CFLAGS_METAL += -DPM_HAS_TCC=1 -DPM_METAL_TCC_LIB_DIR=\"$(FW_TCC_LIB_DIR)\" \
	-I$(TCC_DIR)

EXTERNALS_SH := $(METAL_DIR)/tools/externals.sh
FW_TCC_SRCS := $(shell $(EXTERNALS_SH) list tcc)

ifeq ($(FW_TCC_SRCS),)
$(error fw_tcc.mk: externals.sh list tcc returned nothing)
endif

TCC_DEPS := $(addprefix $(TCC_DIR)/,$(FW_TCC_SRCS))
FW_TCC_OBJ := $(BUILD)/tcc/libtcc.o
FW_OBJS += $(FW_TCC_OBJ)

# The shared one-recipe N-instances macro (tools/tcc_instances.mk) compiles
# libtcc.c ONE_SOURCE per backend. TCC_DEFINES (tcc.mk) derives the host
# triplet; firmware overrides it — a bare-metal build wants no /usr search
# path at all, and CONFIG_TRIPLET only feeds the hosted -run lane.
TCC_DEFINES_FIRMWARE := -DCONFIG_TCC_STATIC=1 -DCONFIG_TCC_SEMLOCK=0 \
	-DCONFIG_TCC_BACKTRACE=0 -DCONFIG_TCC_BCHECK=0
# fwinc shadows win over every system header for the recipe's compile: the
# freestanding TCC takes fwinc's FILE/stdio face, no glibc struct timespec
# clash, no __isoc23_* strtoul renames (tcc_instances.mk's TCC_INC seam).
TCC_INC := -I$(PORT_DIR)/fwinc

include $(METAL_DIR)/tools/tcc_instances.mk

$(eval $(call tcc_instance,$(FW_TCC_TARGET),$(FW_TCC_TARGET_DEFINE),$(FW_TCC_OBJ),,$(TCC_DEFINES_FIRMWARE)))
