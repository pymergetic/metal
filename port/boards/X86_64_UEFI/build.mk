# X86_64_UEFI — PE + freestanding µPy (COM1). Windows COFF ABI + fsys stubs.
# Links with host lld-link or docker (no sudo mingw required).

BOARD_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
PORT_DIR := $(CURDIR)
COMMON := $(PORT_DIR)/common
BUILD ?= build-X86_64_UEFI

ENGINE ?= mp
PACKAGES := $(abspath $(PORT_DIR)/../../../..)
ifeq ($(ENGINE),upy)
ENGINE_TOP := $(PACKAGES)/micropython
else ifeq ($(ENGINE),mpwm)
ENGINE_TOP := $(PACKAGES)/metalpython-wasmmod
else
ENGINE_TOP := $(abspath $(PORT_DIR)/../../..)
endif

ifeq ($(wildcard $(ENGINE_TOP)/py/mkenv.mk),)
$(error ENGINE_TOP=$(ENGINE_TOP) has no py/mkenv.mk (ENGINE=$(ENGINE)))
endif

include $(ENGINE_TOP)/py/mkenv.mk
TOP := $(ENGINE_TOP)

CLANG ?= clang
CC := $(CLANG)
QEMU ?= qemu-system-x86_64
OVMF ?= /usr/share/ovmf/OVMF.fd
EDK_INC ?= $(abspath $(PORT_DIR)/../_tmp/external/edk2/MdePkg/Include)

QSTR_DEFS = $(COMMON)/qstrdefsport.h
MICROPY_ROM_TEXT_COMPRESSION ?= 0

include $(TOP)/py/py.mk

TARGET_WIN := --target=x86_64-unknown-windows

INC := -I$(COMMON) -I$(BOARD_DIR) -I$(TOP) -I$(BUILD) \
	-I$(EDK_INC) -I$(EDK_INC)/X64 \
	-isystem /usr/include -isystem /usr/include/x86_64-linux-gnu

CFLAGS_METAL := $(TARGET_WIN) -ffreestanding -fno-stack-protector \
	-fshort-wchar -mno-red-zone -fno-asynchronous-unwind-tables -fno-exceptions \
	-Wall -Wextra -Wno-unused-parameter -Os -DNDEBUG \
	-fdata-sections -ffunction-sections \
	-std=gnu99 \
	-DMICROPY_HEAP_SIZE=131072

REPL ?= 0
ifeq ($(REPL),1)
CFLAGS_METAL += -DMETAL_UPY_SMOKE=0
else
CFLAGS_METAL += -DMETAL_UPY_SMOKE=1
endif

CFLAGS += $(INC) $(CFLAGS_METAL)
CSUPEROPT = -Os

SRC_C = \
	boards/X86_64_UEFI/main.c \
	boards/X86_64_UEFI/uart.c \
	common/mphalport.c \
	common/main_upy.c \
	common/fsys/chkstk.c \
	shared/readline/readline.c \
	shared/runtime/pyexec.c \
	shared/runtime/stdout_helpers.c \
	shared/libc/printf.c \
	shared/libc/string0.c

SRC_QSTR += shared/readline/readline.c shared/runtime/pyexec.c

OBJ = $(PY_CORE_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))

LLD_LINK := $(shell command -v lld-link 2>/dev/null)
ifeq ($(LLD_LINK),)
LLD_LINK := $(firstword $(wildcard /usr/lib/llvm-*/bin/lld-link))
endif

.PHONY: all run clean

all: $(BUILD)/esp/EFI/BOOT/BOOTX64.EFI

$(BUILD):
	$(MKDIR) -p $@

$(BUILD)/BOOTX64.EFI: $(OBJ) | $(BUILD)
	$(ECHO) "LINK $@"
	@if [ -n "$(LLD_LINK)" ]; then \
	  $(LLD_LINK) -subsystem:efi_application -entry:UefiMain -out:$@ $(OBJ); \
	else \
	  echo "note: lld-link via docker"; \
	  printf '%s\n' $(OBJ) | sed 's|^$(BUILD)/||' > $(BUILD)/obj.rsp; \
	  docker run --rm -v $(abspath $(BUILD)):/b -w /b ubuntu:24.04 bash -lc '\
	    set -euo pipefail; \
	    export DEBIAN_FRONTEND=noninteractive; \
	    apt-get update -qq; \
	    apt-get install -y -qq lld >/tmp/apt.log; \
	    mapfile -t objs < obj.rsp; \
	    lld-link -subsystem:efi_application -entry:UefiMain -out:BOOTX64.EFI "$${objs[@]}"'; \
	fi

$(BUILD)/esp/EFI/BOOT/BOOTX64.EFI: $(BUILD)/BOOTX64.EFI
	$(MKDIR) -p $(BUILD)/esp/EFI/BOOT
	cp -f $< $@

run: $(BUILD)/esp/EFI/BOOT/BOOTX64.EFI
	@test -f "$(OVMF)" || { echo "FAIL: OVMF missing at $(OVMF)"; exit 1; }
	@set +e; \
	rm -f $(BUILD)/serial.log; \
	$(QEMU) -machine q35,accel=kvm:tcg -m 256 -vga none \
		-display none -serial file:$(BUILD)/serial.log \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF) \
		-drive format=raw,file=fat:rw:$(BUILD)/esp & \
	qpid=$$!; \
	ok=0; \
	for i in $$(seq 1 120); do \
	  if grep -a -q "upy ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "ovmf ok" $(BUILD)/serial.log 2>/dev/null; then ok=1; break; fi; \
	  if ! kill -0 $$qpid 2>/dev/null; then break; fi; \
	  sleep 0.1; \
	done; \
	kill -KILL $$qpid 2>/dev/null; wait $$qpid 2>/dev/null; \
	echo "----- serial (trimmed) -----"; \
	grep -a -E "metal |upy ok|ovmf ok|BdsDxe: (loading|starting) Boot0001" $(BUILD)/serial.log 2>/dev/null || true; \
	if [ $$ok -eq 1 ]; then echo "X86_64_UEFI_UPY_OK ENGINE=$(ENGINE)"; exit 0; fi; \
	echo "X86_64_UEFI_UPY_FAIL"; \
	tail -c 1600 $(BUILD)/serial.log 2>/dev/null || true; \
	exit 1

clean:
	$(RM) -rf $(BUILD)

include $(TOP)/py/mkrules.mk
