# X86_64_BIOS — Multiboot trampoline + freestanding µPy (COM1).
#
#   make -C ports/metal BOARD=X86_64_BIOS ENGINE=mp
#   make -C ports/metal BOARD=X86_64_BIOS run

BOARD_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
# make -f is always invoked with cwd = metalmod/port
PORT_DIR := $(CURDIR)
COMMON := $(PORT_DIR)/common
BUILD ?= build-X86_64_BIOS

ENGINE ?= mp
PACKAGES := $(abspath $(PORT_DIR)/../../../..)
ifeq ($(ENGINE),upy)
ENGINE_TOP := $(PACKAGES)/micropython
else ifeq ($(ENGINE),mpwm)
ENGINE_TOP := $(PACKAGES)/metalpython-wasmmod
else
# port → metalmod → extmod → metalpython
ENGINE_TOP := $(abspath $(PORT_DIR)/../../..)
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

INC := -I$(COMMON) -I$(BOARD_DIR) -I$(TOP) -I$(BUILD)

CFLAGS_METAL := -m64 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-mno-red-zone -fno-asynchronous-unwind-tables -fno-exceptions \
	-Wall -Wextra -Wno-unused-parameter -Os -DNDEBUG \
	-fdata-sections -ffunction-sections \
	-std=gnu99 \
	-DMICROPY_HEAP_SIZE=131072 \
	-DMETAL_UPY_SMOKE=1

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
	shared/readline/readline.c \
	shared/runtime/pyexec.c \
	shared/runtime/stdout_helpers.c \
	shared/libc/printf.c \
	shared/libc/string0.c

SRC_QSTR += shared/readline/readline.c shared/runtime/pyexec.c

OBJ = $(PY_CORE_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))

LIBGCC := $(shell $(CC) $(CFLAGS_METAL) -print-libgcc-file-name)

.PHONY: all run clean

all: $(BUILD)/metal.qemu.elf

$(BUILD):
	$(MKDIR) -p $@

$(BUILD)/crt0.o: boards/X86_64_BIOS/crt0.S | $(BUILD)
	$(ECHO) "AS $<"
	$(Q)$(CC) $(ASFLAGS64) -c -o $@ $<

$(BUILD)/metal.elf: $(BUILD)/crt0.o $(OBJ) boards/X86_64_BIOS/link.ld
	$(ECHO) "LINK $@"
	$(Q)$(LD) -m elf_x86_64 -nostdlib -T boards/X86_64_BIOS/link.ld \
		--gc-sections -o $@ $(BUILD)/crt0.o $(OBJ) $(LIBGCC)
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
	if grep -q "upy ok" $(BUILD)/serial.log && grep -q "qemu ok" $(BUILD)/serial.log; then \
	  echo "X86_64_BIOS_UPY_OK ENGINE=$(ENGINE)"; \
	  exit 0; \
	fi; \
	echo "X86_64_BIOS_UPY_FAIL (qemu ec=$$ec)"; \
	exit 1

clean:
	$(RM) -rf $(BUILD)

include $(TOP)/py/mkrules.mk
