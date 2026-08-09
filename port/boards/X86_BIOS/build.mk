# X86_BIOS — Multiboot ELF32 + freestanding µPy (COM1), i686 seat.
#
#   make -C ports/metal BOARD=X86_BIOS ENGINE=mp
#   make -C ports/metal BOARD=X86_BIOS run

BOARD_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
# make -f is always invoked with cwd = metal/port
PORT_DIR := $(CURDIR)
UPY := $(PORT_DIR)/upy
BOOT := $(PORT_DIR)/boot
LIVE_DIR := $(PORT_DIR)/live
BRINGUP := $(PORT_DIR)/bringup
METAL := $(abspath $(PORT_DIR)/..)
GLUE := $(METAL)/glue
COMMON := $(UPY)
BUILD ?= build/X86_BIOS-$(ENGINE)

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

# WAMR ia32 freestanding port not wired yet — keep off for i686 bring-up.
override LINK_WAMR := 0

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
QEMU ?= qemu-system-i386
# Host provisioning only — guest N comes from ACPI MADT (prove 2 and 4).
SMP ?= 2
TFTP_ROOT := $(LIVE_DIR)/tftp-root
SSH_BANNER := $(LIVE_DIR)/qemu-ssh-banner.sh
# SLIRP TFTP + guestfwd SSH ident helper at 10.0.2.100:22
NETDEV_USER := user,id=n0,tftp=$(TFTP_ROOT),guestfwd=tcp:10.0.2.100:22-cmd:$(SSH_BANNER)
GFX ?= 1
ifeq ($(GFX),1)
QEMU_VGA := -vga std
else
QEMU_VGA := -vga none
endif

# VNC=1 (default when GFX=1): QEMU listens on 127.0.0.1:5900+N for TightVNC/etc.
# Laptop/SSH: ssh -L 5900:127.0.0.1:5900 host  then connect viewer to localhost:5900
VNC ?= $(GFX)
VNC_DISPLAY ?= 0
ifeq ($(VNC),1)
QEMU_DISPLAY := -display none -vnc 127.0.0.1:$(VNC_DISPLAY)
else
QEMU_DISPLAY := -display none
endif
QEMU_MACHINE := -machine q35,accel=kvm:tcg -m 256 -smp $(SMP) $(QEMU_VGA)

QSTR_DEFS = $(UPY)/qstrdefsport.h
MICROPY_ROM_TEXT_COMPRESSION ?= 0
FROZEN_MANIFEST ?= $(PORT_DIR)/manifest.py
MICROPY_MANIFEST_METAL := $(METAL)

include $(TOP)/py/py.mk

INC := -I$(UPY) -I$(BOOT) -I$(LIVE_DIR) -I$(BRINGUP) -I$(GLUE) -I$(PORT_DIR)/hal -I$(BOARD_DIR) -I$(TOP) -I$(BUILD) \
	-I$(METAL)/include -I$(METAL)/src -I$(METAL)/third_party/tlsf

# Master bake = official realm. Lab/own-realm: override METAL_CDN_URL /
# METAL_CDN_EXTRA_URLS at make time (DHCP opt 224 can still add/replace later).
METAL_CDN_URL ?= https://cdn.pymergetic.com/cdn
METAL_CDN_EXTRA_URLS ?=

# REPL=1 → interactive friendly REPL (no auto isa-debug-exit smoke path)
REPL ?= 0
ifeq ($(REPL),1)
MICROPY_HEAP_SIZE ?= 1048576
CFLAGS_METAL_SMOKE := -DMETAL_UPY_SMOKE=0
else
MICROPY_HEAP_SIZE ?= 262144
CFLAGS_METAL_SMOKE := -DMETAL_UPY_SMOKE=1
endif

CFLAGS_METAL := -DMETAL_BOARD_UEFI=0 -DPM_METAL_BOARD_BIOS=1 -m32 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-fno-asynchronous-unwind-tables -fno-exceptions \
	-Wall -Wextra -Wno-unused-parameter -Os -DNDEBUG \
	-fdata-sections -ffunction-sections \
	-std=gnu99 \
	-DMICROPY_HEAP_SIZE=$(MICROPY_HEAP_SIZE) \
	$(CFLAGS_METAL_SMOKE) \
	-DPM_METAL_CFG_ARCH_X86=1 -DPM_METAL_CFG_FW_BIOS=1 \
	-DMETAL_LINK_WAMR=$(LINK_WAMR) \
	-DMETAL_ENGINE=\"$(ENGINE)\" \
	-DMETAL_CDN_URL=\"$(METAL_CDN_URL)\" \
	-DMETAL_CDN_EXTRA_URLS=\"$(METAL_CDN_EXTRA_URLS)\"

# LIVE=1 → after smoke stay up serving HTTP on :80 (needs hostfwd)
# LIVE_SSH=1 → after smoke stay up sending SSH banner on :22
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
	-I$(METAL)/src/pymergetic/metal/wamr_host/port/platform \
	-I$(METAL)/include/pymergetic/metal/libc
CFLAGS_METAL += -DBH_PLATFORM_METAL
endif

CFLAGS += $(INC) $(CFLAGS_METAL) -DMICROPY_PY_LWIP=1
CSUPEROPT = -Os

ASFLAGS32 := -m32

# Paths relative to PORT_DIR (make -f is invoked with cwd = PORT_DIR)
SRC_C = \
	boards/X86_BIOS/main.c \
	boards/X86_BIOS/uart.c \
	hal/bios/console.c \
	upy/mphalport.c \
	upy/main_upy.c \
	boot/boot.c \
	boot/autoexec.c \
	boot/cdn_cfg.c \
	bringup/product_bringup.c \
	bringup/metal_board_time.c \
	live/floor_smoke.c \
	live/net_smoke.c \
	live/ip_smoke.c \
	live/console_smoke.c \
	live/draw_smoke.c \
	live/vt_smoke.c \
	live/tui_smoke.c \
	live/kbd_smoke.c \
	boot/services.c \
	live/live_http.c \
	live/live_ssh.c \
	bringup/uefi_acpi_seed.c \
	bringup/network_metal_nic.c \
	upy/inspect_py.c \
	shared/readline/readline.c \
	shared/runtime/pyexec.c \
	shared/runtime/stdout_helpers.c \
	shared/libc/printf.c \
	shared/netutils/netutils.c \
	extmod/modframebuf.c \
	extmod/modnetwork.c \
	extmod/modlwip.c \
	extmod/network_lwip.c \
	extmod/modasyncio.c \
	extmod/modjson.c \
	extmod/modre.c \
	extmod/modtime.c \
	extmod/modselect.c

SRC_C += live/metal_log.c
ifeq ($(LINK_WAMR),1)
SRC_C += live/wamr_smoke.c
else
SRC_C += shared/libc/string0.c
endif

# Freestanding Rust rt — custom i686 target JSON (no prebuilt rust-std).
export RUST_TARGET_PATH := $(METAL)
RUST_TARGET := i686-unknown-none
# Custom JSON target needs nightly + build-std + -Zunstable-options.
export RUSTFLAGS := -Zunstable-options $(RUSTFLAGS)
CARGO := cargo +nightly -Z build-std=core,alloc,compiler_builtins -Z build-std-features=compiler-builtins-mem
include $(PORT_DIR)/rust_product.mk
include $(PORT_DIR)/product_packs.mk

include $(PORT_DIR)/glue_src.mk


SRC_QSTR += shared/readline/readline.c shared/runtime/pyexec.c extmod/modframebuf.c \
	extmod/modnetwork.c extmod/modlwip.c extmod/network_lwip.c extmod/modasyncio.c extmod/modjson.c \
	extmod/modre.c extmod/modtime.c extmod/modselect.c bringup/network_metal_nic.c

OBJ = $(PY_CORE_O)
OBJ += $(BUILD)/frozen_content.o
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))
OBJ += $(BUILD)/metal_mem.o $(BUILD)/metal_tlsf.o $(BUILD)/metal_async.o $(BUILD)/metal_smp.o $(BUILD)/metal_ap_tramp.o $(BUILD)/metal_acpi.o $(BUILD)/metal_asgi.o $(BUILD)/metal_inspect.o $(BUILD)/metal_console.o
OBJ += $(BUILD)/metal_mod_packs.o $(BUILD)/metal_pack_inspect.o $(BUILD)/metal_pack_metal.o
OBJ += $(BUILD)/metal_boot_tree.o $(BUILD)/metal_externals.o $(BUILD)/metal_externals_rows.o $(BUILD)/metal_arch.o $(BUILD)/metal_ascii.o
OBJ += $(BUILD)/metal_auth.o $(BUILD)/metal_trust.o $(BUILD)/metal_endian.o $(BUILD)/metal_fourcc.o $(BUILD)/metal_eightcc.o
OBJ += $(BUILD)/metal_draw.o $(BUILD)/metal_vt.o $(BUILD)/metal_tui.o $(BUILD)/metal_kbd.o
OBJ += $(BUILD)/metal_serial.o $(BUILD)/metal_shell_ui.o
OBJ += $(BUILD)/metal_scanout.o $(BUILD)/metal_scanout_virtio_gpu.o $(BUILD)/metal_scanout_bochs.o
OBJ += $(BUILD)/metal_scanout_radeon.o $(BUILD)/metal_scanout_i915.o
OBJ += $(BUILD)/metal_scanout_gop_blt.o $(BUILD)/metal_scanout_lfb.o $(BUILD)/metal_gop_port.o 
OBJ += $(BUILD)/metal_gfx_compositor.o $(BUILD)/metal_gfx_text.o
OBJ += $(BUILD)/metal_fb_harvest.o $(BUILD)/metal_gfx_bringup.o $(BUILD)/metal_nic_bringup.o
OBJ += $(BUILD)/metal_bge_metal.o $(BUILD)/metal_bge_netif.o $(BUILD)/metal_bge_port.o
OBJ += $(BUILD)/metal_pci.o $(BUILD)/metal_virtio_pci.o $(BUILD)/metal_virtio_net.o
OBJ += $(BUILD)/metal_http.o $(BUILD)/metal_ssh.o $(BUILD)/metal_dhcp.o
OBJ += $(BUILD)/metal_dns.o $(BUILD)/metal_ntp.o $(BUILD)/metal_tftp.o $(BUILD)/metal_faces.o $(BUILD)/metal_nic.o
OBJ += $(BUILD)/metal_net_pump.o $(BUILD)/metal_ssh_pkt.o
OBJ += $(BUILD)/metal_ssh_crypto.o $(BUILD)/metal_ssh_kex.o
OBJ += $(BUILD)/metal_monocypher.o $(BUILD)/metal_monocypher_ed25519.o $(BUILD)/metal_sha256.o

$(eval $(call GLUE_ATTACH))

include $(PORT_DIR)/lwip.mk

# mbedTLS + Metal net/tls (appends CFLAGS -I and OBJ)
include $(PORT_DIR)/mbedtls.mk

SSH_CRYPTO_INC := -I$(METAL)/third_party/monocypher -I$(METAL)/third_party/sha256

WAMR_LIB :=
ifeq ($(LINK_WAMR),1)
WAMR_LIB := $(BUILD)/wamr-fs/libwasmmod_wamr_freestanding.a
OBJ += $(BUILD)/metal_platform.o $(BUILD)/metal_libc_stdlib.o $(BUILD)/metal_libc_string.o
endif

RUST_LIBS := $(METAL_RT_LIB)

$(BUILD)/metal_mem.o: $(METAL)/src/pymergetic/metal/mem/port/mem.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_tlsf.o: $(METAL)/third_party/tlsf/tlsf.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_async.o: $(METAL)/src/pymergetic/metal/async/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_smp.o: $(METAL)/src/pymergetic/metal/async/smp.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_ap_tramp.o: boards/X86_BIOS/ap_trampoline32.S | $(BUILD)
	$(ECHO) "AS $<"
	$(Q)$(CC) $(ASFLAGS32) -c -o $@ $<

$(BUILD)/metal_acpi.o: $(METAL)/src/pymergetic/metal/dev/acpi/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_asgi.o: $(METAL)/src/pymergetic/metal/net/asgi/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -I$(METAL)/src/pymergetic/metal/net/asgi -c -o $@ $<

$(BUILD)/metal_boot_tree.o: $(METAL)/src/pymergetic/metal/boot/tree.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_externals.o: $(METAL)/src/pymergetic/metal/boot/externals.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_externals_rows.o: $(METAL)/src/pymergetic/metal/boot/externals_rows.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_arch.o: $(METAL)/src/pymergetic/metal/arch/arch.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_ascii.o: $(METAL)/src/pymergetic/metal/util/ascii.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_auth.o: $(METAL)/src/pymergetic/metal/auth/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -I$(METAL)/third_party/monocypher -c -o $@ $<

$(BUILD)/metal_trust.o: $(METAL)/src/pymergetic/metal/trust/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -I$(METAL)/third_party/monocypher -c -o $@ $<

$(BUILD)/metal_endian.o: $(METAL)/src/pymergetic/metal/util/endian/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_fourcc.o: $(METAL)/src/pymergetic/metal/util/fourcc/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_eightcc.o: $(METAL)/src/pymergetic/metal/util/eightcc/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<



$(BUILD)/metal_mod_packs.o: $(METAL)/src/pymergetic/metal/pack/mod_packs.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_inspect.o: $(METAL)/src/pymergetic/metal/inspect/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_console.o: $(METAL)/src/pymergetic/metal/console/__init__.c | $(BUILD)
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

$(BUILD)/metal_serial.o: $(METAL)/src/pymergetic/metal/dev/serial/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_shell_ui.o: $(METAL)/src/pymergetic/metal/shell/ui/viewport.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_scanout.o: $(METAL)/src/pymergetic/metal/dev/gfx/scanout.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_scanout_virtio_gpu.o: $(METAL)/src/pymergetic/metal/dev/gfx/scanout_virtio_gpu.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_scanout_bochs.o: $(METAL)/src/pymergetic/metal/dev/gfx/scanout_bochs.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_scanout_radeon.o: $(METAL)/src/pymergetic/metal/dev/gfx/scanout_radeon_rv370.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_scanout_i915.o: $(METAL)/src/pymergetic/metal/dev/gfx/scanout_i915_855gm.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_scanout_gop_blt.o: $(METAL)/src/pymergetic/metal/dev/gfx/scanout_gop_blt.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_scanout_lfb.o: $(METAL)/src/pymergetic/metal/dev/gfx/scanout_lfb_copy.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_gfx_compositor.o: $(METAL)/src/pymergetic/metal/dev/gfx/compositor.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_gfx_text.o: $(METAL)/src/pymergetic/metal/dev/gfx/text.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_gop_port.o: $(METAL)/src/pymergetic/metal/boot/platform/bios/gop_blt_port.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -DPM_METAL_BOOT_TARGET_BIOS=1 -c -o $@ $<

$(BUILD)/metal_fb_harvest.o: $(BOARD_DIR)/fb_harvest.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_gfx_bringup.o: $(BRINGUP)/gfx_bringup.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_nic_bringup.o: $(BRINGUP)/nic_bringup.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_bge_metal.o: $(METAL)/src/pymergetic/metal/dev/net/bge/bge_metal.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -I$(METAL)/src/pymergetic/metal/dev/net/bge -c -o $@ $<

$(BUILD)/metal_bge_netif.o: $(METAL)/src/pymergetic/metal/dev/net/bge/bge_netif.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -I$(METAL)/src/pymergetic/metal/dev/net/bge -c -o $@ $<

$(BUILD)/metal_bge_port.o: $(METAL)/src/pymergetic/metal/dev/net/bge/bge_port.c | $(BUILD)
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

$(BUILD)/metal_nic.o: $(METAL)/src/pymergetic/metal/net/nic/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_net_pump.o: $(METAL)/src/pymergetic/metal/net/pump/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_ssh_pkt.o: $(METAL)/src/pymergetic/metal/net/ssh/packet.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) $(SSH_CRYPTO_INC) -c -o $@ $<

ifeq ($(LINK_WAMR),1)
$(BUILD)/metal_platform.o: $(METAL)/src/pymergetic/metal/wamr_host/port/platform/metal_platform.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -I$(WASMMOD)/third_party/wamr/core/shared/platform/include \
		-c -o $@ $<

$(BUILD)/metal_libc_stdlib.o: $(METAL)/src/pymergetic/metal/libc/port/stdlib.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -nostdinc -I$(METAL)/include/pymergetic/metal/libc -c -o $@ $<

$(BUILD)/metal_libc_stdio.o: $(METAL)/src/pymergetic/metal/libc/port/stdio.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -nostdinc -I$(METAL)/include/pymergetic/metal/libc -c -o $@ $<

$(BUILD)/metal_libc_string.o: $(METAL)/src/pymergetic/metal/libc/port/string.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -nostdinc -I$(METAL)/include/pymergetic/metal/libc -c -o $@ $<

$(WAMR_LIB): $(WASMMOD)/ports/metal/wamr_freestanding.mk
	$(ECHO) "WAMR freestanding $@"
	$(Q)$(MAKE) -f $(WASMMOD)/ports/metal/wamr_freestanding.mk \
		OUT_DIR=$(BUILD)/wamr-fs \
		WAMR_DIR=$(WASMMOD)/third_party/wamr \
		METAL_PLAT_INC=$(METAL)/src/pymergetic/metal/wamr_host/port/platform \
		METAL_PORT_INC=$(METAL)/src/pymergetic/metal/wamr_host/port \
		METAL_LIBC_INC=$(METAL)/include/pymergetic/metal/libc \
		METAL_SRC_INC=$(METAL) \
		METAL_INCLUDE_INC=$(METAL)/include \
		UEFI=0
endif

LIBGCC := $(shell $(CC) $(CFLAGS_METAL) -print-libgcc-file-name)

.PHONY: all run clean live-http live-ssh

all: $(BUILD)/metal.qemu.elf

$(BUILD):
	$(MKDIR) -p $@

$(BUILD)/crt0.o: boards/X86_BIOS/crt0.S | $(BUILD)
	$(ECHO) "AS $<"
	$(Q)$(CC) $(ASFLAGS32) -c -o $@ $<

$(BUILD)/metal.elf: $(BUILD)/crt0.o $(OBJ) $(WAMR_LIB) $(RUST_LIBS) boards/X86_BIOS/link.ld
	$(ECHO) "LINK $@"
	$(Q)$(LD) -m elf_i386 -nostdlib -z noexecstack -T boards/X86_BIOS/link.ld \
		--gc-sections \
		-u pm_metal_rt_halt -u pm_metal_rt_connect_symbols \
		-u pm_metal_fs_wasmmod_mount_mpwp -u pm_metal_fs_read_async \
		-u pm_metal_mod_packs_mount_all \
		-o $@ $(BUILD)/crt0.o $(OBJ) $(WAMR_LIB) \
		--whole-archive $(RUST_LIBS) --no-whole-archive $(LIBGCC)
	$(Q)$(SIZE) $@
	$(Q)nm -g $@ | grep -Eq '[[:space:]]T[[:space:]]+pm_metal_rt_halt$$' \
		|| (echo "FAIL: pm_metal_rt_halt missing (Rust rt not linked)" >&2; exit 1)
	$(Q)nm -g $@ | grep -Eq '[[:space:]]T[[:space:]]+pm_metal_fs_wasmmod_mount_mpwp$$' \
		|| (echo "FAIL: pm_metal_fs_wasmmod_mount_mpwp missing (Rust fs not linked)" >&2; exit 1)

# Single Multiboot ELF32 — qemu -kernel uses the same image.
$(BUILD)/metal.qemu.elf: $(BUILD)/metal.elf
	$(Q)cp -f $< $@

# Product REPL: lean bring-up + friendly µPy on COM1 (not the smoke battery).
# Stdin feeder waits for DHCP, then sends print(1+1); expect banner + >>> + result.
repl: $(BUILD)/metal.qemu.elf
	@test "$(REPL)" = "1" || { echo "repl requires REPL=1"; exit 1; }
	@set +e; \
	rm -f $(BUILD)/serial.log; \
	timeout 25s bash -c '\
	  ( sleep 6; printf "print(1+1)\r\n"; sleep 2; printf "\x04"; sleep 1; ) \
	  | $(QEMU) $(QEMU_MACHINE) \
		-netdev $(NETDEV_USER) -device virtio-net-pci,netdev=n0 \
		$(QEMU_DISPLAY) -serial stdio -monitor none \
		-kernel $(BUILD)/metal.qemu.elf \
		>$(BUILD)/serial.log 2>&1'; \
	ec=$$?; \
	echo "----- serial -----"; \
	tr -d '\r' <$(BUILD)/serial.log | grep -E "metal |bringup|repl|>>>|MicroPython|^2$$|print|Traceback|AttributeError|Metal Python" | tail -50 || true; \
	if grep -a -q "MetalPython" $(BUILD)/serial.log \
	  && grep -a -q ">>>" $(BUILD)/serial.log \
	  && tr -d '\r' <$(BUILD)/serial.log | grep -qx "2" \
	  && ! tr -d '\r' <$(BUILD)/serial.log | grep -q "AttributeError"; then \
	  echo "X86_BIOS_REPL_OK ENGINE=$(ENGINE)"; exit 0; \
	fi; \
	echo "X86_BIOS_REPL_FAIL ENGINE=$(ENGINE) qemu_ec=$$ec"; \
	tail -c 2500 $(BUILD)/serial.log 2>/dev/null || true; \
	exit 1

run: $(BUILD)/metal.qemu.elf
	@set +e; \
	$(QEMU) $(QEMU_MACHINE) \
		-device isa-debug-exit,iobase=0x501,iosize=0x02 \
		-netdev $(NETDEV_USER),hostfwd=tcp::22022-:22 -device virtio-net-pci,netdev=n0 \
		$(QEMU_DISPLAY) -serial file:$(BUILD)/serial.log \
		-kernel $(BUILD)/metal.qemu.elf; \
	ec=$$?; \
	echo "----- serial -----"; \
	cat $(BUILD)/serial.log; \
	if grep -q "console ok" $(BUILD)/serial.log \
	  && grep -q "floor ok" $(BUILD)/serial.log \
	  && grep -q "net ok" $(BUILD)/serial.log \
	  && grep -q "dhcp ok" $(BUILD)/serial.log \
	  && grep -q "ping ok" $(BUILD)/serial.log \
	  && grep -q "ip ok" $(BUILD)/serial.log \
	  && grep -q "udp ok" $(BUILD)/serial.log \
	  && grep -q "dns ok" $(BUILD)/serial.log \
	  && grep -q "tcp ok" $(BUILD)/serial.log \
	  && grep -q "http ok" $(BUILD)/serial.log \
	  && grep -qE "ssh ok|ssh stub" $(BUILD)/serial.log \
	  && grep -q "http client ok" $(BUILD)/serial.log \
	  && grep -q "ntp ok" $(BUILD)/serial.log \
	  && grep -q "tftp ok" $(BUILD)/serial.log \
	  && grep -q "draw ok" $(BUILD)/serial.log \
	  && grep -q "vt ok" $(BUILD)/serial.log \
	  && grep -q "tui ok" $(BUILD)/serial.log \
	  && grep -q "kbd ok" $(BUILD)/serial.log \
	  && { [ "$(LINK_WAMR)" != "1" ] || grep -q "wamr ok" $(BUILD)/serial.log; } \
	  && grep -q "upy ok" $(BUILD)/serial.log \
	  && grep -q "microdot ok" $(BUILD)/serial.log \
	  && grep -q "framebuf ok" $(BUILD)/serial.log \
	  && grep -q "network ok" $(BUILD)/serial.log \
	  && grep -q "dns py ok" $(BUILD)/serial.log \
	  && grep -q "socket ok" $(BUILD)/serial.log \
	  && grep -qE "ssh py ok|ssh ok|ssh stub" $(BUILD)/serial.log \
	  && grep -q "qemu ok" $(BUILD)/serial.log; then \
	  echo "X86_BIOS_OK ENGINE=$(ENGINE) LINK_WAMR=$(LINK_WAMR)"; \
	  exit 0; \
	fi; \
	echo "X86_BIOS_FAIL (qemu ec=$$ec) ENGINE=$(ENGINE) LINK_WAMR=$(LINK_WAMR)"; \
	exit 1

# LIVE=1 image + hostfwd; curl guest :80 via host :18080.
# Keep :22022-:22 for live-ssh / future sshd hostfwd.
live-http: $(BUILD)/metal.qemu.elf
	@test "$(LIVE)" = "1" || { echo "live-http requires LIVE=1"; exit 1; }
	@set +e; \
	rm -f $(BUILD)/serial.log; \
	$(QEMU) $(QEMU_MACHINE) \
		-netdev $(NETDEV_USER),hostfwd=tcp::18080-:80,hostfwd=tcp::22022-:22 \
		-device virtio-net-pci,netdev=n0 \
		$(QEMU_DISPLAY) -serial file:$(BUILD)/serial.log \
		-kernel $(BUILD)/metal.qemu.elf & \
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
	health=""; caps=""; page=""; self=""; ec1=1; ec2=1; ec3=1; ec4=1; \
	for t in 1 2 3 4 5 6 7 8; do \
	  health=$$(curl -fsS --max-time 3 http://127.0.0.1:18080/health 2>/dev/null); \
	  ec1=$$?; \
	  caps=$$(curl -fsS --max-time 3 http://127.0.0.1:18080/capabilities 2>/dev/null); \
	  ec2=$$?; \
	  page=$$(curl -fsS --max-time 3 http://127.0.0.1:18080/inspect/ 2>/dev/null); \
	  ec3=$$?; \
	  self=$$(curl -fsS --max-time 3 http://127.0.0.1:18080/inspect/self 2>/dev/null); \
	  ec4=$$?; \
	  if [ $$ec1 -eq 0 ] && [ $$ec2 -eq 0 ] && [ $$ec3 -eq 0 ] && [ $$ec4 -eq 0 ] \
	    && echo "$$health" | grep -q '"ok":true' \
	    && echo "$$caps" | grep -q '"role":"metal"' \
	    && echo "$$page" | grep -q '<title>Inspect</title>' \
	    && echo "$$self" | grep -q '"role":"kernel"' \
	    && echo "$$self" | grep -q '"has_source":false'; then \
	    break; \
	  fi; \
	  sleep 0.4; \
	done; \
	kill -KILL $$qpid 2>/dev/null; wait $$qpid 2>/dev/null; \
	echo "----- serial (tail) -----"; \
	grep -a -E "ok|live http|fail|asgi|smp " $(BUILD)/serial.log 2>/dev/null | tail -30 || true; \
	echo "health=[$$health] ec=$$ec1"; \
	echo "caps=[$$caps] ec=$$ec2"; \
	echo "inspect_title=$$(echo "$$page" | tr -d '\r' | grep -o '<title>[^<]*</title>' | head -1) ec=$$ec3"; \
	echo "self=[$$self] ec=$$ec4"; \
	if [ $$ec1 -eq 0 ] && [ $$ec2 -eq 0 ] && [ $$ec3 -eq 0 ] && [ $$ec4 -eq 0 ] \
	  && echo "$$health" | grep -q '"ok":true' \
	  && echo "$$caps" | grep -q '"role":"metal"' \
	  && echo "$$page" | grep -q '<title>Inspect</title>' \
	  && echo "$$self" | grep -q '"role":"kernel"' \
	  && echo "$$self" | grep -q '"has_source":false'; then \
	  echo "X86_BIOS_LIVE_HTTP_OK ENGINE=$(ENGINE)"; \
	  exit 0; \
	fi; \
	echo "X86_BIOS_LIVE_HTTP_FAIL ENGINE=$(ENGINE)"; \
	exit 1

# LIVE_SSH=1 image + hostfwd; guest prints live ssh and sends ident banner on :22.
live-ssh: $(BUILD)/metal.qemu.elf
	@test "$(LIVE_SSH)" = "1" || { echo "live-ssh requires LIVE_SSH=1"; exit 1; }
	@set +e; \
	rm -f $(BUILD)/serial.log; \
	$(QEMU) $(QEMU_MACHINE) \
		-netdev $(NETDEV_USER),hostfwd=tcp::22022-:22 \
		-device virtio-net-pci,netdev=n0 \
		$(QEMU_DISPLAY) -serial file:$(BUILD)/serial.log \
		-kernel $(BUILD)/metal.qemu.elf & \
	qpid=$$!; \
	ok=0; \
	for i in $$(seq 1 300); do \
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
	  echo "X86_BIOS_LIVE_SSH_OK ENGINE=$(ENGINE)"; \
	  exit 0; \
	fi; \
	echo "X86_BIOS_LIVE_SSH_FAIL ENGINE=$(ENGINE)"; \
	exit 1

clean:
	$(RM) -rf $(BUILD)

include $(TOP)/py/mkrules.mk
