# Vendored TCC (externals/tcc), same sources as host/unix. The card (jit.c) calls
# tcc_compile_string(), so this library is linked into every firmware binary.
# TCC_TARGET_X86_64 — the natural compile target for the x86 firmware seats.
# Include after CFLAGS_METAL / INC exist.

TCC_DIR ?= $(METAL_DIR)/externals/tcc
CFLAGS_METAL += -DTCC_TARGET_X86_64 -DPM_HAS_TCC=1 \
	-I$(TCC_DIR) -I$(PORT_DIR) \
	-I$(abspath $(PORT_DIR)/../../..)

FW_TCC_SRCS := \
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

FW_OBJS += $(addprefix $(BUILD)/tcc/, $(FW_TCC_SRCS:.c=.o))

$(BUILD)/tcc/%.o: $(TCC_DIR)/%.c | $(BUILD)/tcc
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/tcc:
	mkdir -p $@