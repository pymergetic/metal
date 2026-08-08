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
SMP ?= 2
TFTP_ROOT := $(COMMON)/tftp-root
SSH_BANNER := $(COMMON)/qemu-ssh-banner.sh
NETDEV_USER := user,id=n0,tftp=$(TFTP_ROOT),guestfwd=tcp:10.0.2.100:22-cmd:$(SSH_BANNER)
QEMU_MACHINE := -machine q35,accel=kvm:tcg -m 256 -smp $(SMP) -vga none
OVMF ?= /usr/share/ovmf/OVMF.fd
# Slim MdePkg headers only (no edk2 submodule — WAMR/µPy live in wasmmod / ENGINE_TOP).
EDK_INC ?= $(BOARD_DIR)/edk_inc

QSTR_DEFS = $(COMMON)/qstrdefsport.h
MICROPY_ROM_TEXT_COMPRESSION ?= 0
FROZEN_MANIFEST ?= $(PORT_DIR)/manifest.py
MICROPY_MANIFEST_METAL := $(METAL)

include $(TOP)/py/py.mk

TARGET_WIN := --target=x86_64-unknown-windows

INC := -I$(COMMON) -I$(BOARD_DIR) -I$(TOP) -I$(BUILD) \
	-I$(METAL)/include -I$(METAL)/src -I$(METAL)/third_party/tlsf \
	-I$(EDK_INC) -I$(EDK_INC)/X64 \
	-isystem /usr/include -isystem /usr/include/x86_64-linux-gnu

CFLAGS_METAL := $(TARGET_WIN) -ffreestanding -fno-stack-protector \
	-fshort-wchar -mno-red-zone -fno-asynchronous-unwind-tables -fno-exceptions \
	-Wall -Wextra -Wno-unused-parameter -Os -DNDEBUG \
	-fdata-sections -ffunction-sections \
	-std=gnu99 \
	-DMICROPY_HEAP_SIZE=196608 \
	-DMETAL_BOARD_UEFI=1 \
	-DMETAL_LINK_WAMR=$(LINK_WAMR) \
	-DMETAL_ENGINE=\"$(ENGINE)\"

REPL ?= 0
ifeq ($(REPL),1)
CFLAGS_METAL += -DMETAL_UPY_SMOKE=0
else
CFLAGS_METAL += -DMETAL_UPY_SMOKE=1
endif

LIVE ?= 0
LIVE_SSH ?= 0
ifeq ($(LIVE_SSH),1)
CFLAGS_METAL += -DMETAL_LIVE_SSH=1 -DMETAL_LIVE=0
else ifeq ($(LIVE),1)
CFLAGS_METAL += -DMETAL_LIVE=1 -DMETAL_LIVE_SSH=0
else
CFLAGS_METAL += -DMETAL_LIVE=0 -DMETAL_LIVE_SSH=0
endif

ifeq ($(LINK_WAMR),1)
INC += -I$(WASMMOD)/third_party/wamr/core/iwasm/include \
	-I$(METAL)/src/pymergetic/metal/wasm/port/platform \
	-I$(METAL)/include/pymergetic/metal/libc
CFLAGS_METAL += -DBH_PLATFORM_METAL
endif

CFLAGS += $(INC) $(CFLAGS_METAL)
CSUPEROPT = -Os

SRC_C = \
	boards/X86_64_UEFI/main.c \
	boards/X86_64_UEFI/uart.c \
	common/mphalport.c \
	common/main_upy.c \
	common/product_bringup.c \
	common/metal_board_time.c \
	common/floor_smoke.c \
	common/net_smoke.c \
	common/ip_smoke.c \
	common/console_smoke.c \
	common/draw_smoke.c \
	common/vt_smoke.c \
	common/tui_smoke.c \
	common/kbd_smoke.c \
	common/live_http.c \
	common/live_ssh.c \
	common/uefi_acpi_seed.c \
	common/network_metal_nic.c \
	common/modssh.c \
	common/fsys/chkstk.c \
	shared/readline/readline.c \
	shared/runtime/pyexec.c \
	shared/runtime/stdout_helpers.c \
	shared/libc/printf.c \
	shared/netutils/netutils.c \
	extmod/modframebuf.c \
	extmod/modnetwork.c \
	extmod/modsocket.c \
	extmod/modasyncio.c \
	extmod/modjson.c \
	extmod/modre.c \
	extmod/modtime.c \
	extmod/modselect.c

ifeq ($(LINK_WAMR),1)
SRC_C += \
	common/wamr_smoke.c \
	common/metal_log.c \
	common/metal_rt_halt.c
else
SRC_C += shared/libc/string0.c
endif

SRC_QSTR += shared/readline/readline.c shared/runtime/pyexec.c extmod/modframebuf.c \
	extmod/modnetwork.c extmod/modsocket.c extmod/modasyncio.c extmod/modjson.c \
	extmod/modre.c extmod/modtime.c extmod/modselect.c common/network_metal_nic.c \
	common/modssh.c

OBJ = $(PY_CORE_O)
OBJ += $(BUILD)/frozen_content.o
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))
OBJ += $(BUILD)/metal_mem.o $(BUILD)/metal_tlsf.o $(BUILD)/metal_async.o $(BUILD)/metal_smp.o $(BUILD)/metal_ap_tramp.o $(BUILD)/metal_acpi.o $(BUILD)/metal_asgi.o $(BUILD)/metal_asgi_static.o $(BUILD)/metal_inspect.o $(BUILD)/metal_console.o
OBJ += $(BUILD)/metal_draw.o $(BUILD)/metal_vt.o $(BUILD)/metal_tui.o $(BUILD)/metal_kbd.o
OBJ += $(BUILD)/metal_pci.o $(BUILD)/metal_virtio_pci.o $(BUILD)/metal_virtio_net.o
OBJ += $(BUILD)/metal_ip.o $(BUILD)/metal_udp.o $(BUILD)/metal_tcp.o $(BUILD)/metal_http.o $(BUILD)/metal_ssh.o $(BUILD)/metal_dhcp.o
OBJ += $(BUILD)/metal_dns.o $(BUILD)/metal_ntp.o $(BUILD)/metal_tftp.o $(BUILD)/metal_faces.o $(BUILD)/metal_upy_nic.o
OBJ += $(BUILD)/metal_net_pump.o $(BUILD)/metal_ssh_pkt.o
OBJ += $(BUILD)/metal_ssh_crypto.o $(BUILD)/metal_ssh_kex.o
OBJ += $(BUILD)/metal_monocypher.o $(BUILD)/metal_monocypher_ed25519.o $(BUILD)/metal_sha256.o

SSH_CRYPTO_INC := -I$(METAL)/third_party/monocypher -I$(METAL)/third_party/sha256

WAMR_LIB :=
ifeq ($(LINK_WAMR),1)
WAMR_LIB := $(BUILD)/wamr-fs/libwasmmod_wamr_freestanding.a
OBJ += $(BUILD)/metal_platform.o $(BUILD)/metal_libc_stdlib.o $(BUILD)/metal_libc_string.o
endif

$(BUILD)/metal_mem.o: $(METAL)/src/pymergetic/metal/mem/port/mem.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_tlsf.o: $(METAL)/third_party/tlsf/tlsf.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_draw.o: $(METAL)/src/pymergetic/metal/draw/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_vt.o: $(METAL)/src/pymergetic/metal/shell/vt/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_tui.o: $(METAL)/src/pymergetic/metal/shell/tui/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_kbd.o: $(METAL)/src/pymergetic/metal/dev/input/kbd.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_async.o: $(METAL)/src/pymergetic/metal/async/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_smp.o: $(METAL)/src/pymergetic/metal/async/smp.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_ap_tramp.o: $(METAL)/src/pymergetic/metal/async/ap_trampoline.S | $(BUILD)
	$(ECHO) "AS $<"
	$(Q)$(CC) $(TARGET_WIN) -c -o $@ $<

$(BUILD)/metal_acpi.o: $(METAL)/src/pymergetic/metal/dev/acpi/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_asgi.o: $(METAL)/src/pymergetic/metal/asgi/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -I$(METAL)/src/pymergetic/metal/asgi -c -o $@ $<

$(BUILD)/metal_asgi_static.o: $(METAL)/src/pymergetic/metal/asgi/static_embed.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -I$(METAL)/src/pymergetic/metal/asgi -c -o $@ $<

$(BUILD)/metal_inspect.o: $(METAL)/src/pymergetic/metal/inspect/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_console.o: $(METAL)/src/pymergetic/metal/console/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_pci.o: $(METAL)/src/pymergetic/metal/bus/pci/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_virtio_pci.o: $(METAL)/src/pymergetic/metal/bus/virtio/virtio_pci.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_virtio_net.o: $(METAL)/src/pymergetic/metal/dev/net/virtio_net.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_ip.o: $(METAL)/src/pymergetic/metal/net/ip/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_udp.o: $(METAL)/src/pymergetic/metal/net/ip/udp.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_tcp.o: $(METAL)/src/pymergetic/metal/net/ip/tcp.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_http.o: $(METAL)/src/pymergetic/metal/net/http/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_ssh.o: $(METAL)/src/pymergetic/metal/net/ssh/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) $(SSH_CRYPTO_INC) -c -o $@ $<

$(BUILD)/metal_ssh_crypto.o: $(METAL)/src/pymergetic/metal/net/ssh/crypto.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) $(SSH_CRYPTO_INC) -c -o $@ $<

$(BUILD)/metal_ssh_kex.o: $(METAL)/src/pymergetic/metal/net/ssh/kex.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) $(SSH_CRYPTO_INC) -c -o $@ $<

$(BUILD)/metal_monocypher.o: $(METAL)/third_party/monocypher/monocypher.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) $(SSH_CRYPTO_INC) -c -o $@ $<

$(BUILD)/metal_monocypher_ed25519.o: $(METAL)/third_party/monocypher/monocypher-ed25519.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) $(SSH_CRYPTO_INC) -c -o $@ $<

$(BUILD)/metal_sha256.o: $(METAL)/third_party/sha256/sha256.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) $(SSH_CRYPTO_INC) -c -o $@ $<

$(BUILD)/metal_dhcp.o: $(METAL)/src/pymergetic/metal/net/dhcp/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_dns.o: $(METAL)/src/pymergetic/metal/net/dns/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_ntp.o: $(METAL)/src/pymergetic/metal/net/ntp/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_tftp.o: $(METAL)/src/pymergetic/metal/net/tftp/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_faces.o: $(METAL)/src/pymergetic/metal/net/faces/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_upy_nic.o: $(METAL)/src/pymergetic/metal/net/upy_nic/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_net_pump.o: $(METAL)/src/pymergetic/metal/net/pump/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_ssh_pkt.o: $(METAL)/src/pymergetic/metal/net/ssh/packet.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) $(SSH_CRYPTO_INC) -c -o $@ $<

ifeq ($(LINK_WAMR),1)
$(BUILD)/metal_platform.o: $(METAL)/src/pymergetic/metal/wasm/port/platform/metal_platform.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -I$(WASMMOD)/third_party/wamr/core/shared/platform/include \
		-c -o $@ $<

$(BUILD)/metal_libc_stdlib.o: $(METAL)/src/pymergetic/metal/libc/port/stdlib.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -nostdinc -I$(METAL)/include/pymergetic/metal/libc -c -o $@ $<

$(BUILD)/metal_libc_string.o: $(METAL)/src/pymergetic/metal/libc/port/string.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -nostdinc -I$(METAL)/include/pymergetic/metal/libc -c -o $@ $<

$(WAMR_LIB): $(WASMMOD)/ports/metal/wamr_freestanding.mk
	$(ECHO) "WAMR freestanding (UEFI) $@"
	$(Q)$(MAKE) -f $(WASMMOD)/ports/metal/wamr_freestanding.mk \
		OUT_DIR=$(BUILD)/wamr-fs \
		WAMR_DIR=$(WASMMOD)/third_party/wamr \
		METAL_PLAT_INC=$(METAL)/src/pymergetic/metal/wasm/port/platform \
		METAL_PORT_INC=$(METAL)/src/pymergetic/metal/wasm/port \
		METAL_LIBC_INC=$(METAL)/include/pymergetic/metal/libc \
		METAL_SRC_INC=$(METAL) \
		METAL_INCLUDE_INC=$(METAL)/include \
		UEFI=1
endif

LLD_LINK := $(shell command -v lld-link 2>/dev/null)
ifeq ($(LLD_LINK),)
LLD_LINK := $(firstword $(wildcard /usr/lib/llvm-*/bin/lld-link))
endif

.PHONY: all run clean live-http live-ssh

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

# Product REPL probe (lean bring-up; see BIOS repl for semantics).
repl: $(BUILD)/esp/EFI/BOOT/BOOTX64.EFI
	@test "$(REPL)" = "1" || { echo "repl requires REPL=1"; exit 1; }
	@test -f "$(OVMF)" || { echo "FAIL: OVMF missing at $(OVMF)"; exit 1; }
	@set +e; \
	rm -f $(BUILD)/serial.log; \
	timeout 40s bash -c '\
	  ( sleep 12; printf "print(1+1)\r\n"; sleep 2; printf "\x04"; sleep 1; ) \
	  | $(QEMU) $(QEMU_MACHINE) \
		-display none -serial stdio -monitor none \
		-netdev $(NETDEV_USER) -device virtio-net-pci,netdev=n0 \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF) \
		-drive format=raw,file=fat:rw:$(BUILD)/esp \
		>$(BUILD)/serial.log 2>&1'; \
	ec=$$?; \
	echo "----- serial (trimmed) -----"; \
	grep -a -E "metal |bringup|repl|>>>|MicroPython|^2$$|print|Traceback" $(BUILD)/serial.log 2>/dev/null | tail -50 || true; \
	if grep -a -q "metal repl" $(BUILD)/serial.log \
	  && tr -d '\r' <$(BUILD)/serial.log | grep -qx "2"; then \
	  echo "X86_64_UEFI_REPL_OK ENGINE=$(ENGINE)"; exit 0; \
	fi; \
	echo "X86_64_UEFI_REPL_FAIL ENGINE=$(ENGINE) qemu_ec=$$ec"; \
	tail -c 2500 $(BUILD)/serial.log 2>/dev/null || true; \
	exit 1

run: $(BUILD)/esp/EFI/BOOT/BOOTX64.EFI
	@test -f "$(OVMF)" || { echo "FAIL: OVMF missing at $(OVMF)"; exit 1; }
	@set +e; \
	rm -f $(BUILD)/serial.log; \
	$(QEMU) $(QEMU_MACHINE) \
		-display none -serial file:$(BUILD)/serial.log \
		-netdev $(NETDEV_USER),hostfwd=tcp::22022-:22 -device virtio-net-pci,netdev=n0 \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF) \
		-drive format=raw,file=fat:rw:$(BUILD)/esp & \
	qpid=$$!; \
	ok=0; \
	for i in $$(seq 1 300); do \
	  if grep -a -q "console ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "floor ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "net ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "dhcp ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "ping ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "ip ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "udp ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "dns ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "tcp ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "http ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -qE "ssh ok|ssh stub" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "http client ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "ntp ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "tftp ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "draw ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "vt ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "tui ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "kbd ok" $(BUILD)/serial.log 2>/dev/null \
	     && { [ "$(LINK_WAMR)" != "1" ] || grep -a -q "wamr ok" $(BUILD)/serial.log 2>/dev/null; } \
	     && grep -a -q "upy ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "microdot ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -qE "framebuf ok|framebuf skip" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "network ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "dns py ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "socket ok" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -qE "ssh py ok|ssh ok|ssh stub" $(BUILD)/serial.log 2>/dev/null \
	     && grep -a -q "ovmf ok" $(BUILD)/serial.log 2>/dev/null; then ok=1; break; fi; \
	  if ! kill -0 $$qpid 2>/dev/null; then break; fi; \
	  sleep 0.1; \
	done; \
	kill -KILL $$qpid 2>/dev/null; wait $$qpid 2>/dev/null; \
	echo "----- serial (trimmed) -----"; \
	grep -a -E "metal |console ok|floor ok|net ok|dhcp ok|ping ok|ip ok|udp ok|dns ok|tcp ok|http ok|ssh ok|ssh stub|http client ok|ntp ok|tftp ok|draw ok|vt ok|tui ok|kbd ok|wamr ok|framebuf ok|framebuf skip|network ok|dns py ok|socket ok|ssh py ok|microdot ok|upy ok|ovmf ok|BdsDxe: (loading|starting) Boot0001" $(BUILD)/serial.log 2>/dev/null || true; \
	if [ $$ok -eq 1 ]; then echo "X86_64_UEFI_OK ENGINE=$(ENGINE) LINK_WAMR=$(LINK_WAMR) LLD=$(LLD_LINK)"; exit 0; fi; \
	echo "X86_64_UEFI_FAIL ENGINE=$(ENGINE) LINK_WAMR=$(LINK_WAMR)"; \
	tail -c 1600 $(BUILD)/serial.log 2>/dev/null || true; \
	exit 1

# LIVE=1 image + hostfwd; curl guest :80 via host :18080 (mirror BIOS live-http).
# Keep :22022-:22 for live-ssh / future sshd hostfwd.
live-http: $(BUILD)/esp/EFI/BOOT/BOOTX64.EFI
	@test "$(LIVE)" = "1" || { echo "live-http requires LIVE=1"; exit 1; }
	@test -f "$(OVMF)" || { echo "FAIL: OVMF missing at $(OVMF)"; exit 1; }
	@set +e; \
	rm -f $(BUILD)/serial.log; \
	$(QEMU) $(QEMU_MACHINE) \
		-display none -serial file:$(BUILD)/serial.log \
		-netdev $(NETDEV_USER),hostfwd=tcp::18080-:80,hostfwd=tcp::22022-:22 \
		-device virtio-net-pci,netdev=n0 \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF) \
		-drive format=raw,file=fat:rw:$(BUILD)/esp & \
	qpid=$$!; \
	ok=0; \
	for i in $$(seq 1 400); do \
	  if grep -a -q "live http" $(BUILD)/serial.log 2>/dev/null; then ok=1; break; fi; \
	  if ! kill -0 $$qpid 2>/dev/null; then break; fi; \
	  sleep 0.1; \
	done; \
	if [ $$ok -ne 1 ]; then \
	  kill -KILL $$qpid 2>/dev/null; wait $$qpid 2>/dev/null; \
	  echo "live-http: guest never reached live http"; \
	  tail -c 2000 $(BUILD)/serial.log 2>/dev/null || true; \
	  exit 1; \
	fi; \
	health=""; caps=""; page=""; ec1=1; ec2=1; ec3=1; \
	for t in 1 2 3 4 5 6 7 8; do \
	  health=$$(curl -fsS --max-time 3 http://127.0.0.1:18080/health 2>/dev/null); \
	  ec1=$$?; \
	  caps=$$(curl -fsS --max-time 3 http://127.0.0.1:18080/capabilities 2>/dev/null); \
	  ec2=$$?; \
	  page=$$(curl -fsS --max-time 3 http://127.0.0.1:18080/inspect/ 2>/dev/null); \
	  ec3=$$?; \
	  if [ $$ec1 -eq 0 ] && [ $$ec2 -eq 0 ] && [ $$ec3 -eq 0 ] \
	    && echo "$$health" | grep -q '"ok":true' \
	    && echo "$$caps" | grep -q '"role":"metal"' \
	    && echo "$$page" | grep -q '<title>Inspect</title>'; then \
	    break; \
	  fi; \
	  sleep 0.4; \
	done; \
	kill -KILL $$qpid 2>/dev/null; wait $$qpid 2>/dev/null; \
	echo "----- serial (tail) -----"; \
	grep -a -E "ok|live http|fail|ovmf ok|smp " $(BUILD)/serial.log 2>/dev/null | tail -30 || true; \
	echo "health=[$$health] ec=$$ec1"; \
	echo "caps=[$$caps] ec=$$ec2"; \
	echo "inspect_title=$$(echo "$$page" | tr -d '\r' | grep -o '<title>[^<]*</title>' | head -1) ec=$$ec3"; \
	if [ $$ec1 -eq 0 ] && [ $$ec2 -eq 0 ] && [ $$ec3 -eq 0 ] \
	  && echo "$$health" | grep -q '"ok":true' \
	  && echo "$$caps" | grep -q '"role":"metal"' \
	  && echo "$$page" | grep -q '<title>Inspect</title>'; then \
	  echo "X86_64_UEFI_LIVE_HTTP_OK ENGINE=$(ENGINE)"; \
	  exit 0; \
	fi; \
	echo "X86_64_UEFI_LIVE_HTTP_FAIL ENGINE=$(ENGINE)"; \
	exit 1

# LIVE_SSH=1 image + hostfwd; guest prints live ssh and sends ident banner on :22.
live-ssh: $(BUILD)/esp/EFI/BOOT/BOOTX64.EFI
	@test "$(LIVE_SSH)" = "1" || { echo "live-ssh requires LIVE_SSH=1"; exit 1; }
	@test -f "$(OVMF)" || { echo "FAIL: OVMF missing at $(OVMF)"; exit 1; }
	@set +e; \
	rm -f $(BUILD)/serial.log; \
	$(QEMU) $(QEMU_MACHINE) \
		-display none -serial file:$(BUILD)/serial.log \
		-netdev $(NETDEV_USER),hostfwd=tcp::22022-:22 \
		-device virtio-net-pci,netdev=n0 \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF) \
		-drive format=raw,file=fat:rw:$(BUILD)/esp & \
	qpid=$$!; \
	ok=0; \
	for i in $$(seq 1 500); do \
	  if grep -a -q "live ssh" $(BUILD)/serial.log 2>/dev/null; then ok=1; break; fi; \
	  if ! kill -0 $$qpid 2>/dev/null; then break; fi; \
	  sleep 0.1; \
	done; \
	if [ $$ok -ne 1 ]; then \
	  kill -KILL $$qpid 2>/dev/null; wait $$qpid 2>/dev/null; \
	  echo "live-ssh: guest never reached live ssh"; \
	  tail -c 2000 $(BUILD)/serial.log 2>/dev/null || true; \
	  exit 1; \
	fi; \
	auth_ok=0; \
	ask=$$(mktemp); \
	printf '%s\n' '#!/bin/sh' 'echo metal' > $$ask; chmod 700 $$ask; \
	if command -v ssh >/dev/null 2>&1; then \
	  for t in 1 2 3 4 5 6 7 8 9 10; do \
	    if command -v sshpass >/dev/null 2>&1; then \
	      timeout 12 sshpass -p metal ssh -o StrictHostKeyChecking=no \
	        -o UserKnownHostsFile=/dev/null -o PreferredAuthentications=password \
	        -o PubkeyAuthentication=no -o NumberOfPasswordPrompts=1 \
	        -o ConnectTimeout=5 -o ConnectionAttempts=1 \
	        -p 22022 metal@127.0.0.1 true >/dev/null 2>&1 && auth_ok=1; \
	    else \
	      SSH_ASKPASS=$$ask SSH_ASKPASS_REQUIRE=force DISPLAY=:0 \
	        timeout 12 setsid -w ssh -o StrictHostKeyChecking=no \
	        -o UserKnownHostsFile=/dev/null -o PreferredAuthentications=password \
	        -o PubkeyAuthentication=no -o NumberOfPasswordPrompts=1 \
	        -o ConnectTimeout=5 -o ConnectionAttempts=1 \
	        -p 22022 metal@127.0.0.1 true >/dev/null 2>&1 && auth_ok=1; \
	    fi; \
	    if [ $$auth_ok -eq 1 ]; then break; fi; \
	    sleep 0.5; \
	  done; \
	fi; \
	rm -f $$ask; \
	kill -KILL $$qpid 2>/dev/null; wait $$qpid 2>/dev/null; \
	echo "----- serial (tail) -----"; \
	grep -a -E "ok|live ssh|fail|smp " $(BUILD)/serial.log 2>/dev/null | tail -40 || true; \
	echo "ssh auth_ok=$$auth_ok"; \
	if [ $$auth_ok -eq 1 ]; then \
	  echo "X86_64_UEFI_LIVE_SSH_OK ENGINE=$(ENGINE)"; \
	  exit 0; \
	fi; \
	echo "X86_64_UEFI_LIVE_SSH_FAIL ENGINE=$(ENGINE)"; \
	exit 1

clean:
	$(RM) -rf $(BUILD)

include $(TOP)/py/mkrules.mk
