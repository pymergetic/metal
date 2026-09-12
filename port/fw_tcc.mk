# Vendored TCC (externals/tcc) on the firmware seats — the in-kernel compile
# face (jit.c object path: every seat builds objects for every arch per the
# matrix — the board's native backend plus the cross lanes below).
#
# Include from a board build.mk AFTER CFLAGS_METAL/INC exist and BEFORE
# fw_cards.mk (jit.c must compile with PM_HAS_TCC=1 and the LIB_DIR).
#
# One TCC instance = one libtcc.c ONE_SOURCE compile via the shared recipe
# (tools/tcc_instances.mk). FW_TCC_TARGET selects the NATIVE backend define
# (default x86_64; the armv7 boards set the ARM triplet). FW_TCC_CROSS is
# the cross lane list — every other arch this board emits objects for
# (space-separated, from: wasm32 arm x86_64; empty = native-only board).
# FW_TCC_LIB_DIR feeds jit.c's tcc_set_lib_path/tcc_add_library_path so TCC
# never searches /usr/lib.
#
# Cross instances on firmware are the same prefixed libtcc.c objects the
# hosted seats link (tools/tcc_instances.mk): every defined global renamed
# pm_tccw_/pm_tcca_/pm_tccx_, called only through jit.c's prefix-macro
# shims. The prefix pass runs on the board's own toolchain objects (ELF32/
# ELF64/COFF — plain nm/objcopy read them all; the browser seat is the one
# that needs wasm_prefix_syms.py because emcc emits relocatable wasm).
# TCC_NM/TCC_OBJCOPY default to the llvm twins — firmware boards have no
# GNU binutils dependency, and llvm-nm/objcopy parse every object format
# this tree links.
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
# Headers too, for the same reason tools/tcc.mk lists them on the hosted seats:
# the manifest is .c only and libtcc.c is a ONE_SOURCE unity build, so an edit
# to tcc.h or libtcc.h reaches every instance object while changing none of the
# listed prerequisites. This board does not read tools/tcc.mk (it derives the
# host triplet, which a cross build has no use for), so the list is spelled
# here — same variable name, same meaning, read by the shared recipe.
TCC_HDRS := $(wildcard $(TCC_DIR)/*.h)
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
# The recipe's libtcc.c compile must land in the SAME arch as the board
# (tcc_instances.mk's TCC_CFLAGS seam). Hosted seats leave CC at default
# because host==target; a firmware board with a cross clang (armv7) must
# pass the same --target/-m flags the board itself uses, else the object
# is host-arch and lld refuses it at link ("incompatible with crt0.o").
# UEFI overrides TCC_CFLAGS itself (the COFF/PE seam — keep that override
# whole; it already carries its --target).
ifeq ($(origin TCC_CFLAGS),undefined)
TCC_CFLAGS := $(filter --target=% -m% -march=%,$(CFLAGS_METAL))
endif

# The prefix pass reads nm output of the board toolchain's objects — the
# llvm twins parse every format this tree links (ELF32/ELF64/COFF).
TCC_NM ?= llvm-nm-18
TCC_OBJCOPY ?= llvm-objcopy-18

include $(METAL_DIR)/tools/tcc_instances.mk

$(eval $(call tcc_instance,$(FW_TCC_TARGET),$(FW_TCC_TARGET_DEFINE),$(FW_TCC_OBJ),,$(TCC_DEFINES_FIRMWARE)))

# Cross lanes: every arch in FW_TCC_CROSS becomes a second prefixed libtcc
# instance + the jit.c gate define that routes object_compile(target=N)
# to it. The instance objects ride FW_OBJS like the native one.
#   wasm32 -> pm_tccw_ / PM_METAL_TCC_CROSS_WASM32  (serialized module out)
#   arm    -> pm_tcca_ / PM_METAL_TCC_CROSS_ARM_EABI (ET_REL ELF32)
#   x86_64 -> pm_tccx_ / PM_METAL_TCC_CROSS_X86_64   (ET_REL ELF64)
# The native backend is never re-declared as a cross (the router sends
# TARGET_SEAT to the native instance), so a cross equal to FW_TCC_TARGET
# is skipped — one board, one instance per backend, no duplicate objects.
ifneq ($(filter wasm32,$(FW_TCC_CROSS)),)
ifneq ($(FW_TCC_TARGET),wasm32)
FW_TCC_CROSS_WASM_OBJ := $(BUILD)/tcc/libtcc_wasm_cross.o
FW_OBJS += $(FW_TCC_CROSS_WASM_OBJ)
CFLAGS_METAL += -DPM_METAL_TCC_CROSS_WASM32=1
$(eval $(call tcc_instance,wasm32_cross,TCC_TARGET_WASM32,$(FW_TCC_CROSS_WASM_OBJ),pm_tccw_,$(TCC_DEFINES_FIRMWARE)))
endif
endif
ifneq ($(filter arm,$(FW_TCC_CROSS)),)
ifneq ($(FW_TCC_TARGET),arm)
FW_TCC_CROSS_ARM_OBJ := $(BUILD)/tcc/libtcc_arm_cross.o
FW_OBJS += $(FW_TCC_CROSS_ARM_OBJ)
CFLAGS_METAL += -DPM_METAL_TCC_CROSS_ARM_EABI=1
$(eval $(call tcc_instance,arm_eabi_cross,TCC_TARGET_ARM,$(FW_TCC_CROSS_ARM_OBJ),pm_tcca_,$(TCC_DEFINES_FIRMWARE)))
endif
endif
ifneq ($(filter x86_64,$(FW_TCC_CROSS)),)
ifneq ($(FW_TCC_TARGET),x86_64)
FW_TCC_CROSS_X64_OBJ := $(BUILD)/tcc/libtcc_x64_cross.o
FW_OBJS += $(FW_TCC_CROSS_X64_OBJ)
CFLAGS_METAL += -DPM_METAL_TCC_CROSS_X86_64=1
$(eval $(call tcc_instance,x86_64_cross,TCC_TARGET_X86_64,$(FW_TCC_CROSS_X64_OBJ),pm_tccx_,$(TCC_DEFINES_FIRMWARE)))
endif
endif
