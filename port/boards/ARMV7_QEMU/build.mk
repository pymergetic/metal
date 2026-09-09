# ARMV7_QEMU — QEMU virt machine, cortex-a7. The armv7 QEMU prove seat:
# every prove the x64 firmware boards run (drivers, upy CDN autoexec,
# in-kernel TCC object compile) on arm hardware, no device attached.
#
#   make -C extmod/metal/port BOARD=ARMV7_QEMU         (build)
#   make -C extmod/metal/port BOARD=ARMV7_QEMU prove   (serial markers)
#   make -C extmod/metal/port BOARD=ARMV7_QEMU run     (interactive)

BOARD_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
PORT_DIR := $(CURDIR)
METAL_DIR := $(abspath $(PORT_DIR)/..)
WASMMOD := $(abspath $(PORT_DIR)/../../wasmmod)
METAL_SRC := $(METAL_DIR)/src
WASMMOD_SRC := $(WASMMOD)/src
BUILD ?= build/ARMV7_QEMU-mp
CLANG ?= clang
CC := $(CLANG)
LD ?= ld.lld
OBJCOPY ?= llvm-objcopy-18
RUSTC ?= rustc
QEMU ?= qemu-system-arm
CLANG_TARGET := armv7-none-eabihf

CFLAGS_METAL := --target=$(CLANG_TARGET) -marm -mfpu=neon-vfpv4 -mfloat-abi=hard \
	-ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-fno-asynchronous-unwind-tables -fno-exceptions \
	-Wall -Wextra -Wno-unused-parameter -Os -DNDEBUG -std=gnu99 \
	-DPM_METAL_FIRMWARE=1 -DPM_WASMMOD_GUEST=0 -DPM_WASMMOD_IO_FILE=0 \
	-DPM_METAL_UPY_CDN=1 \
	-DPM_METAL_PCI_ECAM_BASE=0x3f000000u \
	-DPM_METAL_PCI_ECAM_BUS_MAX=16u \
	-DPM_METAL_PCI_MMIO_BASE=0x10000000u \
	-DPM_METAL_PCI_MMIO_SIZE=0x2eff0000u \
	-DMICROPY_NLR_SETJMP=1 \
	-DMICROPY_HW_MCU_NAME='"virt-a7"'
ifeq ($(REPL),1)
CFLAGS_METAL += -DPM_METAL_UART_REPL=1
endif
INC := -I$(PORT_DIR)/fwinc -I$(BOARD_DIR) -I$(PORT_DIR) -I$(PORT_DIR)/bringup -I$(METAL_SRC) -I$(WASMMOD_SRC) -I$(WASMMOD) \
	-I$(abspath $(PORT_DIR)/../../..)

FW_OBJS := \
	$(BUILD)/crt0.o \
	$(BUILD)/elf_ctors.o \
	$(BUILD)/uart.o \
	$(BUILD)/main.o \
	$(BUILD)/lib.o \
	$(BUILD)/mem.o \
	$(BUILD)/types.o \
	$(BUILD)/tlsf.o \
	$(BUILD)/modboot.o \
	$(BUILD)/smp.o
# Cards are added by fw_cards.mk from the card tree — do not list them here.

FW_RUSTC_TARGET := armv7a-none-eabihf
FW_WAMR_ARCH := armv7
FW_TCC_TARGET := arm
FW_TCC_CROSS := x86_64 wasm32
include $(PORT_DIR)/fw_cdn.mk
include $(PORT_DIR)/fw_mbedtls.mk
include $(PORT_DIR)/fw_zenoh.mk
include $(PORT_DIR)/fw_tcc.mk
include $(PORT_DIR)/fw_cards.mk
include $(PORT_DIR)/fw_wamr.mk
include $(PORT_DIR)/upy.mk

# The live half of the prove: a pack server on the host loopback, which the
# guest reaches at the user-net gateway. Port agreed with upy/firmware_upy_cdn.py.
CDN_PORT ?= 18124
CDN_PACKS := $(WASMMOD)/examples/packs
LIVE_CDN := $(PORT_DIR)/live_cdn.sh $(CDN_PACKS) $(CDN_PORT) $(BUILD)/cdn.log

QEMU_MACHINE := -machine virt,highmem-ecam=off -cpu cortex-a7 -m 1024 -smp 4 \
	-vga none -audio none -display none \
	-netdev user,id=n0 -device virtio-net-pci,disable-legacy=on,netdev=n0 \
	-netdev user,id=n1 -device virtio-net-pci,disable-legacy=on,netdev=n1

.PHONY: all prove run upload clean
all: $(BUILD)/metal.elf

$(BUILD):
	mkdir -p $@

$(BUILD)/crt0.o: $(BOARD_DIR)/crt0.S | $(BUILD)
	$(CC) $(CFLAGS_METAL) -c -o $@ $<

$(BUILD)/elf_ctors.o: $(PORT_DIR)/elf_ctors.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) -c -o $@ $<

$(BUILD)/uart.o: $(BOARD_DIR)/uart.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/main.o: $(BOARD_DIR)/main.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/lib.o: $(PORT_DIR)/lib.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/mem.o: $(WASMMOD_SRC)/pymergetic/util/mem/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/types.o: $(WASMMOD_SRC)/pymergetic/types/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/tlsf.o: $(WASMMOD)/third_party/tlsf/tlsf.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/modboot.o: $(WASMMOD_SRC)/pymergetic/wasmmod/boot/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/smp.o: $(PORT_DIR)/smp/smp_arm.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/metal.elf: $(FW_OBJS) $(FW_WAMR_LIBS) $(BOARD_DIR)/link.ld
	$(CC) $(CFLAGS_METAL) -nostdlib -fuse-ld=lld \
		-Wl,-T,$(BOARD_DIR)/link.ld -Wl,--gc-sections -Wl,-z,norelro \
		-o $@ $(FW_OBJS) $(FW_WAMR_LIBS)

prove: $(BUILD)/metal.elf
	$(LIVE_CDN) $(PORT_DIR)/serial_prove.sh $(BUILD)/serial.log $(BOARD_DIR)/prove_markers.txt \
		$(QEMU) $(QEMU_MACHINE) -serial file:$(BUILD)/serial.log -monitor none -kernel $(BUILD)/metal.elf

run: $(BUILD)/metal.elf
	$(QEMU) $(QEMU_MACHINE) -serial mon:stdio -kernel $(BUILD)/metal.elf

upload: all

clean:
	rm -rf $(BUILD)
