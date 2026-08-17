# Firmware µPy core — included from board build.mk after FW_OBJS is set
# and before the image link rule. Heap is pymergetic.util.mem (GC off).
# Host boot + finder/importhook instantiate via RS loader + freestanding WAMR.
MPWM_TOP := $(abspath $(PORT_DIR)/../../..)
_FW_CC := $(CC)
_FW_LD := $(LD)
_FW_OBJCOPY := $(OBJCOPY)
MICROPY_MPYCROSS := none
MICROPY_MPYCROSS_DEPENDENCY :=
FROZEN_MANIFEST :=
include $(MPWM_TOP)/py/mkenv.mk
CC := $(_FW_CC)
LD := $(_FW_LD)
OBJCOPY := $(_FW_OBJCOPY)
CPP := $(CC) -E

QSTR_DEFS := $(METAL_DIR)/qstrdefs.metal
SRC_UPY_WASMMOD := \
	extmod/metal/modmetal.c \
	extmod/wasmmod/ports/micropython/modpymergetic.c \
	extmod/wasmmod/ports/micropython/modguest.c \
	extmod/wasmmod/ports/micropython/modcdn.c \
	extmod/wasmmod/ports/micropython/importhook.c \
	extmod/wasmmod/ports/micropython/finder.c \
	extmod/wasmmod/ports/micropython/packbind.c \
	extmod/wasmmod/ports/micropython/nativecall.c \
	extmod/wasmmod/ports/micropython/objhandle.c \
	extmod/wasmmod/ports/micropython/hostready.c \
	extmod/wasmmod/ports/common/boot.c \
	extmod/wasmmod/ports/common/load.c \
	extmod/wasmmod/ports/common/memcookie.c \
	extmod/wasmmod/src/pymergetic/wasmmod/pack/manifest.c \
	extmod/wasmmod/src/pymergetic/wasmmod/pack/zlib_env.c \
	extmod/wasmmod/src/pymergetic/wasmmod/pack/format/common/format.c \
	extmod/wasmmod/src/pymergetic/wasmmod/pack/format/wasm/section.c \
	extmod/wasmmod/src/pymergetic/wasmmod/pack/format/aot/section.c \
	extmod/wasmmod/src/pymergetic/wasmmod/pack/format/elf/section.c \
	extmod/wasmmod/src/pymergetic/wasmmod/verify/__impl__.c \
	extmod/metal/port/upy/firmware_upy.c \
	shared/runtime/stdout_helpers.c \
	shared/libc/printf.c
ifneq ($(filter -DPM_METAL_UART_REPL=1,$(CFLAGS_METAL)),)
SRC_UPY_WASMMOD += \
	shared/runtime/pyexec.c \
	shared/readline/readline.c
endif
SRC_QSTR += $(SRC_UPY_WASMMOD)

CFLAGS += $(CFLAGS_METAL) $(INC) -I$(MPWM_TOP) -I$(BUILD) -I$(PORT_DIR) \
	-include $(WASMMOD)/ports/micropython/mpconfig_wasm.h \
	-DMICROPY_PY_WASM=1 -DMICROPY_PY_WASM_GEN=0 -DMICROPY_PY_WASM_ELF=0 \
	-DMICROPY_PY_WASM_FULL=0 \
	-DMICROPY_WASM_VERIFY=0 -DMICROPY_WASM_AOT_VERSION=0 \
	-DMICROPY_WASM_CONTAINERS=\"wasm\" \
	-DMICROPY_CAN_OVERRIDE_BUILTINS=1 -DMICROPY_PY_SYS_PATH=1 \
	-Wno-unused-but-set-variable -Wno-implicit-fallthrough
include $(MPWM_TOP)/py/py.mk

UPY_O := $(PY_CORE_O) $(addprefix $(BUILD)/, $(SRC_UPY_WASMMOD:.c=.o))

$(BUILD)/firmware_upy_src.c: $(PORT_DIR)/upy/firmware_upy_ready.py \
		$(PORT_DIR)/upy/firmware_upy_cdn.py $(PORT_DIR)/embed_bytes.py | $(BUILD)
	python3 $(PORT_DIR)/embed_bytes.py -o $@ \
		--str pm_metal_firmware_upy_ready_py $(PORT_DIR)/upy/firmware_upy_ready.py \
		--str pm_metal_firmware_upy_cdn_py $(PORT_DIR)/upy/firmware_upy_cdn.py

$(BUILD)/firmware_upy_src.o: $(BUILD)/firmware_upy_src.c
	$(CC) $(CFLAGS_METAL) -c -o $@ $<

FW_OBJS += $(BUILD)/firmware_upy_src.o $(UPY_O)
OBJ := $(FW_OBJS)
include $(MPWM_TOP)/py/mkrules.mk
