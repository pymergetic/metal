# Vendored TCC (externals/tcc), same sources as host/unix. The card (jit.c) calls
# tcc_compile_string(), so this library is linked into every firmware binary.
# TCC_TARGET_X86_64 — the natural compile target for the x86 firmware seats.
# Include after CFLAGS_METAL / INC exist.
#
# The manifest's sources are the translation set libtcc.c #includes (ONE_SOURCE):
# one object, every listed file a dependency. Compiling them as separate objects
# needs tcc.c (the CLI driver), which no seat links.

TCC_DIR ?= $(METAL_DIR)/externals/tcc
CFLAGS_METAL += -DTCC_TARGET_X86_64 -DPM_HAS_TCC=1 \
	-I$(TCC_DIR) -I$(PORT_DIR) \
	-I$(abspath $(PORT_DIR)/../../..)

EXTERNALS_SH := $(METAL_DIR)/tools/externals.sh
FW_TCC_SRCS := $(shell $(EXTERNALS_SH) list tcc)

ifeq ($(FW_TCC_SRCS),)
$(error fw_tcc.mk: externals.sh list tcc returned nothing)
endif

FW_TCC_DEPS := $(addprefix $(TCC_DIR)/,$(FW_TCC_SRCS))
FW_OBJS += $(BUILD)/tcc/libtcc.o

$(BUILD)/tcc/libtcc.o: $(FW_TCC_DEPS) | $(BUILD)/tcc
	$(CC) $(CFLAGS_METAL) $(INC) -std=gnu11 -Wno-unused-parameter -Wno-sign-compare -c -o $@ $(TCC_DIR)/libtcc.c

$(BUILD)/tcc:
	mkdir -p $@