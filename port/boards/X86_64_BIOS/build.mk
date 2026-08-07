# X86_64_BIOS — Multiboot trampoline + freestanding µPy (COM1).
#
#   make -C ports/metal BOARD=X86_64_BIOS ENGINE=mp
#   make -C ports/metal BOARD=X86_64_BIOS run

BOARD_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
# make -f is always invoked with cwd = metal/port
PORT_DIR := $(CURDIR)
COMMON := $(PORT_DIR)/common
METAL := $(abspath $(PORT_DIR)/..)
BUILD ?= build-X86_64_BIOS-$(ENGINE)

ENGINE ?= mp
PACKAGES := $(abspath $(PORT_DIR)/../../../..)
WASMMOD ?= $(abspath $(PORT_DIR)/../../wasmmod)
ifeq ($(wildcard $(WASMMOD)/ports/metal/wamr_freestanding.mk),)
WASMMOD := $(PACKAGES)/metalpython/extmod/wasmmod
endif
ifeq ($(ENGINE),upy)
ENGINE_TOP := $(PACKAGES)/micropython
LINK_WAMR := 0
else ifeq ($(ENGINE),mpwm)
ENGINE_TOP := $(PACKAGES)/metalpython-wasmmod
LINK_WAMR := 1
else
# port → metal → extmod → metalpython
ENGINE_TOP := $(abspath $(PORT_DIR)/../../..)
LINK_WAMR := 1
endif

ifeq ($(wildcard $(ENGINE_TOP)/py/mkenv.mk),)
$(error ENGINE_TOP=$(ENGINE_TOP) has no py/mkenv.mk (ENGINE=$(ENGINE)))
endif

include $(ENGINE_TOP)/py/mkenv.mk
# mkenv sets TOP from its path; keep ENGINE_TOP as the selected tree
TOP := $(ENGINE_TOP)

CLANG ?= clang
CC := $(CLANG)
CXX := $(CLANG)
LD ?= ld
OBJCOPY ?= objcopy
QEMU ?= qemu-system-x86_64

QSTR_DEFS = $(COMMON)/qstrdefsport.h
MICROPY_ROM_TEXT_COMPRESSION ?= 0

include $(TOP)/py/py.mk

INC := -I$(COMMON) -I$(BOARD_DIR) -I$(TOP) -I$(BUILD) \
	-I$(METAL)/include -I$(METAL)/third_party/tlsf

CFLAGS_METAL := -m64 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-mno-red-zone -fno-asynchronous-unwind-tables -fno-exceptions \
	-Wall -Wextra -Wno-unused-parameter -Os -DNDEBUG \
	-fdata-sections -ffunction-sections \
	-std=gnu99 \
	-DMICROPY_HEAP_SIZE=131072 \
	-DMETAL_LINK_WAMR=$(LINK_WAMR)

# REPL=1 → interactive friendly REPL (no auto isa-debug-exit smoke path)
REPL ?= 0
ifeq ($(REPL),1)
CFLAGS_METAL += -DMETAL_UPY_SMOKE=0
else
CFLAGS_METAL += -DMETAL_UPY_SMOKE=1
endif

ifeq ($(LINK_WAMR),1)
INC += -I$(WASMMOD)/third_party/wamr/core/iwasm/include \
	-I$(METAL)/wasm/port/platform \
	-I$(METAL)/libc
CFLAGS_METAL += -DBH_PLATFORM_METAL
endif

CFLAGS += $(INC) $(CFLAGS_METAL)
CSUPEROPT = -Os

ASFLAGS64 := -m64
CFLAGS32 := -m32 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -Wall -Wextra -O2
ASFLAGS32 := -m32

# Paths relative to PORT_DIR (make -f is invoked with cwd = PORT_DIR)
SRC_C = \
	boards/X86_64_BIOS/main.c \
	boards/X86_64_BIOS/uart.c \
	common/mphalport.c \
	common/main_upy.c \
	common/metal_board_time.c \
	common/floor_smoke.c \
	common/console_smoke.c \
	common/draw_smoke.c \
	common/vt_smoke.c \
	shared/readline/readline.c \
	shared/runtime/pyexec.c \
	shared/runtime/stdout_helpers.c \
	shared/libc/printf.c \
	extmod/modframebuf.c

ifeq ($(LINK_WAMR),1)
SRC_C += \
	common/wamr_smoke.c \
	common/metal_log.c \
	common/metal_rt_halt.c
else
SRC_C += shared/libc/string0.c
endif

SRC_QSTR += shared/readline/readline.c shared/runtime/pyexec.c extmod/modframebuf.c

OBJ = $(PY_CORE_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))
OBJ += $(BUILD)/metal_mem.o $(BUILD)/metal_tlsf.o $(BUILD)/metal_async.o $(BUILD)/metal_console.o
OBJ += $(BUILD)/metal_draw.o $(BUILD)/metal_vt.o

WAMR_LIB :=
ifeq ($(LINK_WAMR),1)
WAMR_LIB := $(BUILD)/wamr-fs/libwasmmod_wamr_freestanding.a
OBJ += $(BUILD)/metal_platform.o $(BUILD)/metal_libc_stdlib.o $(BUILD)/metal_libc_string.o
endif

$(BUILD)/metal_mem.o: $(METAL)/mem/mem.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_tlsf.o: $(METAL)/third_party/tlsf/tlsf.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_async.o: $(METAL)/async/async.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_console.o: $(METAL)/console/console.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_draw.o: $(METAL)/draw/draw.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_vt.o: $(METAL)/shell/vt/vt.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

ifeq ($(LINK_WAMR),1)
$(BUILD)/metal_platform.o: $(METAL)/wasm/port/platform/metal_platform.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -I$(WASMMOD)/third_party/wamr/core/shared/platform/include \
		-c -o $@ $<

$(BUILD)/metal_libc_stdlib.o: $(METAL)/libc/stdlib.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -nostdinc -I$(METAL)/libc -c -o $@ $<

$(BUILD)/metal_libc_string.o: $(METAL)/libc/string.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -nostdinc -I$(METAL)/libc -c -o $@ $<

$(WAMR_LIB): $(WASMMOD)/ports/metal/wamr_freestanding.mk
	$(ECHO) "WAMR freestanding $@"
	$(Q)$(MAKE) -f $(WASMMOD)/ports/metal/wamr_freestanding.mk \
		OUT_DIR=$(BUILD)/wamr-fs \
		WAMR_DIR=$(WASMMOD)/third_party/wamr \
		METAL_PLAT_INC=$(METAL)/wasm/port/platform \
		METAL_PORT_INC=$(METAL)/wasm/port \
		METAL_LIBC_INC=$(METAL)/libc \
		METAL_SRC_INC=$(METAL) \
		METAL_INCLUDE_INC=$(METAL)/include \
		UEFI=0
endif

LIBGCC := $(shell $(CC) $(CFLAGS_METAL) -print-libgcc-file-name)

.PHONY: all run clean

all: $(BUILD)/metal.qemu.elf

$(BUILD):
	$(MKDIR) -p $@

$(BUILD)/crt0.o: boards/X86_64_BIOS/crt0.S | $(BUILD)
	$(ECHO) "AS $<"
	$(Q)$(CC) $(ASFLAGS64) -c -o $@ $<

$(BUILD)/metal.elf: $(BUILD)/crt0.o $(OBJ) $(WAMR_LIB) boards/X86_64_BIOS/link.ld
	$(ECHO) "LINK $@"
	$(Q)$(LD) -m elf_x86_64 -nostdlib -T boards/X86_64_BIOS/link.ld \
		--gc-sections -o $@ $(BUILD)/crt0.o $(OBJ) $(WAMR_LIB) $(LIBGCC)
	$(Q)$(SIZE) $@

$(BUILD)/metal_elf.o: $(BUILD)/metal.elf | $(BUILD)
	cd $(BUILD) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 metal.elf metal_elf.o

$(BUILD)/trampoline32.o: boards/X86_64_BIOS/trampoline32.S | $(BUILD)
	$(Q)$(CC) $(ASFLAGS32) -c -o $@ $<

$(BUILD)/trampoline_load.o: boards/X86_64_BIOS/trampoline_load.c | $(BUILD)
	$(Q)$(CC) $(CFLAGS32) -c -o $@ $<

$(BUILD)/trampoline64.o64: boards/X86_64_BIOS/trampoline64.S | $(BUILD)
	$(Q)$(CC) $(ASFLAGS64) -c -o $@ $<

$(BUILD)/trampoline64.o: $(BUILD)/trampoline64.o64 | $(BUILD)
	$(Q)$(OBJCOPY) -O elf32-i386 $< $@

$(BUILD)/metal.qemu.elf: $(BUILD)/trampoline32.o $(BUILD)/trampoline64.o \
		$(BUILD)/trampoline_load.o $(BUILD)/metal_elf.o boards/X86_64_BIOS/link32.ld
	$(Q)$(LD) -m elf_i386 -nostdlib -T boards/X86_64_BIOS/link32.ld -o $@ \
		$(BUILD)/trampoline32.o $(BUILD)/trampoline64.o \
		$(BUILD)/trampoline_load.o $(BUILD)/metal_elf.o

run: $(BUILD)/metal.qemu.elf
	@set +e; \
	$(QEMU) -machine q35,accel=kvm:tcg -m 256 -vga none \
		-device isa-debug-exit,iobase=0x501,iosize=0x02 \
		-display none -serial file:$(BUILD)/serial.log \
		-kernel $(BUILD)/metal.qemu.elf; \
	ec=$$?; \
	echo "----- serial -----"; \
	cat $(BUILD)/serial.log; \
	if grep -q "console ok" $(BUILD)/serial.log \
	  && grep -q "floor ok" $(BUILD)/serial.log \
	  && grep -q "draw ok" $(BUILD)/serial.log \
	  && grep -q "vt ok" $(BUILD)/serial.log \
	  && { [ "$(LINK_WAMR)" != "1" ] || grep -q "wamr ok" $(BUILD)/serial.log; } \
	  && grep -q "upy ok" $(BUILD)/serial.log \
	  && grep -q "framebuf ok" $(BUILD)/serial.log \
	  && grep -q "qemu ok" $(BUILD)/serial.log; then \
	  echo "X86_64_BIOS_OK ENGINE=$(ENGINE) LINK_WAMR=$(LINK_WAMR)"; \
	  exit 0; \
	fi; \
	echo "X86_64_BIOS_FAIL (qemu ec=$$ec) ENGINE=$(ENGINE) LINK_WAMR=$(LINK_WAMR)"; \
	exit 1

clean:
	$(RM) -rf $(BUILD)

include $(TOP)/py/mkrules.mk
