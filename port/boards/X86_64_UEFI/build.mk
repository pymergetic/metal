# X86_64_UEFI — PE + freestanding µPy (COM1). Windows COFF ABI + fsys stubs.
# Links with host lld-link or docker (no sudo mingw required).

BOARD_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
PORT_DIR := $(CURDIR)
COMMON := $(PORT_DIR)/common
METAL := $(abspath $(PORT_DIR)/..)
BUILD ?= build-X86_64_UEFI-$(ENGINE)

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
ENGINE_TOP := $(abspath $(PORT_DIR)/../../..)
LINK_WAMR := 1
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
	-I$(METAL)/include -I$(METAL)/third_party/tlsf \
	-I$(EDK_INC) -I$(EDK_INC)/X64 \
	-isystem /usr/include -isystem /usr/include/x86_64-linux-gnu

CFLAGS_METAL := $(TARGET_WIN) -ffreestanding -fno-stack-protector \
	-fshort-wchar -mno-red-zone -fno-asynchronous-unwind-tables -fno-exceptions \
	-Wall -Wextra -Wno-unused-parameter -Os -DNDEBUG \
	-fdata-sections -ffunction-sections \
	-std=gnu99 \
	-DMICROPY_HEAP_SIZE=131072 \
	-DMETAL_LINK_WAMR=$(LINK_WAMR) \
	-DMETAL_ENGINE=\"$(ENGINE)\"

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

SRC_C = \
	boards/X86_64_UEFI/main.c \
	boards/X86_64_UEFI/uart.c \
	common/mphalport.c \
	common/main_upy.c \
	common/metal_board_time.c \
	common/floor_smoke.c \
	common/net_smoke.c \
	common/ip_smoke.c \
	common/console_smoke.c \
	common/draw_smoke.c \
	common/vt_smoke.c \
	common/tui_smoke.c \
	common/kbd_smoke.c \
	common/fsys/chkstk.c \
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
OBJ += $(BUILD)/metal_draw.o $(BUILD)/metal_vt.o $(BUILD)/metal_tui.o $(BUILD)/metal_kbd.o
OBJ += $(BUILD)/metal_pci.o $(BUILD)/metal_virtio_pci.o $(BUILD)/metal_virtio_net.o
OBJ += $(BUILD)/metal_ip.o $(BUILD)/metal_udp.o $(BUILD)/metal_tcp.o $(BUILD)/metal_http.o

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

$(BUILD)/metal_draw.o: $(METAL)/draw/draw.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_vt.o: $(METAL)/shell/vt/vt.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_tui.o: $(METAL)/shell/tui/tui.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_kbd.o: $(METAL)/dev/input/kbd.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_async.o: $(METAL)/async/async.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_console.o: $(METAL)/console/console.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_pci.o: $(METAL)/bus/pci/pci.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_virtio_pci.o: $(METAL)/bus/virtio/virtio_pci.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_virtio_net.o: $(METAL)/dev/net/virtio_net.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_ip.o: $(METAL)/net/ip/ip.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_udp.o: $(METAL)/net/ip/udp.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_tcp.o: $(METAL)/net/ip/tcp.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_http.o: $(METAL)/net/http/http.c | $(BUILD)
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
	$(ECHO) "WAMR freestanding (UEFI) $@"
	$(Q)$(MAKE) -f $(WASMMOD)/ports/metal/wamr_freestanding.mk \
		OUT_DIR=$(BUILD)/wamr-fs \
		WAMR_DIR=$(WASMMOD)/third_party/wamr \
		METAL_PLAT_INC=$(METAL)/wasm/port/platform \
		METAL_PORT_INC=$(METAL)/wasm/port \
		METAL_LIBC_INC=$(METAL)/libc \
		METAL_SRC_INC=$(METAL) \
		METAL_INCLUDE_INC=$(METAL)/include \
		UEFI=1
endif

LLD_LINK := $(shell command -v lld-link 2>/dev/null)
ifeq ($(LLD_LINK),)
LLD_LINK := $(firstword $(wildcard /usr/lib/llvm-*/bin/lld-link))
endif

.PHONY: all run clean

all: $(BUILD)/esp/EFI/BOOT/BOOTX64.EFI

$(BUILD):
	$(MKDIR) -p $@

$(BUILD)/BOOTX64.EFI: $(OBJ) $(WAMR_LIB) | $(BUILD)
	$(ECHO) "LINK $@"
	@if [ -n "$(LLD_LINK)" ]; then \
	  $(LLD_LINK) -subsystem:efi_application -entry:UefiMain -out:$@ $(OBJ) $(WAMR_LIB); \
	else \
	  echo "note: lld-link via docker"; \
	  printf '%s\n' $(OBJ) $(WAMR_LIB) | sed 's|^$(BUILD)/||' > $(BUILD)/obj.rsp; \
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
		-netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF) \
		-drive format=raw,file=fat:rw:$(BUILD)/esp & \
	qpid=$$!; \
	ok=0; \
	for i in $$(seq 1 120); do \
	  if grep -a -q "console ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "floor ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "net ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "ip ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "udp ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "tcp ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "http ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "draw ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "vt ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "tui ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "kbd ok" $(BUILD)/serial.log 2>/dev/null \
	     && { [ "$(LINK_WAMR)" != "1" ] || grep -a -q "wamr ok" $(BUILD)/serial.log 2>/dev/null; } \
	     && grep -a -q "upy ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "framebuf ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "ovmf ok" $(BUILD)/serial.log 2>/dev/null; then ok=1; break; fi; \
	  if ! kill -0 $$qpid 2>/dev/null; then break; fi; \
	  sleep 0.1; \
	done; \
	kill -KILL $$qpid 2>/dev/null; wait $$qpid 2>/dev/null; \
	echo "----- serial (trimmed) -----"; \
	grep -a -E "metal |console ok|floor ok|net ok|ip ok|udp ok|tcp ok|http ok|draw ok|vt ok|tui ok|kbd ok|wamr ok|framebuf ok|upy ok|ovmf ok|BdsDxe: (loading|starting) Boot0001" $(BUILD)/serial.log 2>/dev/null || true; \
	if [ $$ok -eq 1 ]; then echo "X86_64_UEFI_OK ENGINE=$(ENGINE) LINK_WAMR=$(LINK_WAMR) LLD=$(LLD_LINK)"; exit 0; fi; \
	echo "X86_64_UEFI_FAIL ENGINE=$(ENGINE) LINK_WAMR=$(LINK_WAMR)"; \
	tail -c 1600 $(BUILD)/serial.log 2>/dev/null || true; \
	exit 1

clean:
	$(RM) -rf $(BUILD)

include $(TOP)/py/mkrules.mk
