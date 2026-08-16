# X86_64_UEFI — PE EFI via clang-windows + lld-link (GetMemoryMap → CLASS_MEM heap).
#
# Same cards as X86_64_BIOS. Constructors (.CRT$XCU) call add() like browser
# .init_array; crt.c walks them (no GNU start/stop).
BOARD_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
BIOS_DIR := $(BOARD_DIR)/../X86_64_BIOS
PORT_DIR := $(CURDIR)
METAL_DIR := $(abspath $(PORT_DIR)/..)
WASMMOD := $(abspath $(PORT_DIR)/../../wasmmod)
METAL_SRC := $(METAL_DIR)/src
WASMMOD_SRC := $(WASMMOD)/src
BUILD ?= build/X86_64_UEFI-mp-repl
CLANG ?= clang
CC := $(CLANG)
LLD_LINK ?= lld-link
QEMU ?= qemu-system-x86_64
RUSTC ?= rustc
OVMF ?= /usr/share/ovmf/OVMF.fd

CFLAGS_METAL := --target=x86_64-unknown-windows -ffreestanding -fno-stack-protector \
	-fno-stack-check -fno-strict-aliasing -fno-asynchronous-unwind-tables \
	-mno-stack-arg-probe -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
	-Wall -Wno-unused-parameter -Os -DNDEBUG -std=gnu99 \
	-DPM_METAL_FIRMWARE=1 -DPM_WASMMOD_GUEST=1 -DPM_METAL_UEFI=1
INC := -I$(PORT_DIR)/fwinc -I$(PORT_DIR) -I$(BIOS_DIR) -I$(PORT_DIR)/bringup -I$(METAL_SRC) -I$(WASMMOD_SRC) -I$(WASMMOD) \
	-I$(abspath $(PORT_DIR)/../../..)

$(BUILD)/esp.img: $(BUILD)/esp/EFI/BOOT/BOOTX64.EFI
	dd if=/dev/zero of=$@ bs=1M count=8 status=none
	mkfs.vfat -n ESP $@ >/dev/null
	mmd -i $@ ::EFI ::EFI/BOOT
	mcopy -i $@ $< ::EFI/BOOT/BOOTX64.EFI

QEMU_MACHINE := -machine q35,accel=kvm:tcg -cpu max,-svm -m 128 -smp 1 \
	-vga none -audio none -display none \
	-bios $(OVMF) \
	-device isa-debug-exit,iobase=0x501,iosize=0x02 \
	-netdev user,id=n0 -device virtio-net-pci,disable-legacy=on,netdev=n0 \
	-netdev user,id=n1 -device virtio-net-pci,disable-legacy=on,netdev=n1 \
	-drive file=$(BUILD)/cake.img,if=none,format=raw,id=d0 \
	-device virtio-blk-pci,disable-legacy=on,drive=d0 \
	-drive file=$(BUILD)/esp.img,if=none,format=raw,id=esp \
	-device virtio-blk-pci,disable-legacy=on,drive=esp,bootindex=0

FW_OBJS := \
	$(BUILD)/crt.o \
	$(BUILD)/uart.o \
	$(BUILD)/main.o \
	$(BUILD)/lib.o \
	$(BUILD)/mem.o \
	$(BUILD)/tlsf.o \
	$(BUILD)/dt.o \
	$(BUILD)/pci.o \
	$(BUILD)/virtio_bus.o \
	$(BUILD)/drivers.o \
	$(BUILD)/drivers_net.o \
	$(BUILD)/drivers_blk.o \
	$(BUILD)/drivers_rtc.o \
	$(BUILD)/blk_virtio.o \
	$(BUILD)/net_virtio.o \
	$(BUILD)/rtc_cmos.o \
	$(BUILD)/memmap.o \
	$(BUILD)/async.o \
	$(BUILD)/ip.o \
	$(BUILD)/modboot.o

FW_RUSTC_TARGET := x86_64-unknown-uefi
FW_WAMR_UEFI := 1
include $(PORT_DIR)/fw_cdn.mk
include $(PORT_DIR)/fw_wamr.mk
include $(PORT_DIR)/upy.mk

.PHONY: all run clean
all: $(BUILD)/esp/EFI/BOOT/BOOTX64.EFI

$(BUILD):
	mkdir -p $@ $(BUILD)/esp/EFI/BOOT

$(BUILD)/crt.o: $(BOARD_DIR)/crt.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) -c -o $@ $<

$(BUILD)/uart.o: $(BIOS_DIR)/uart.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/main.o: $(BIOS_DIR)/main.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/lib.o: $(PORT_DIR)/lib.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) -I$(PORT_DIR)/fwinc -c -o $@ $<

$(BUILD)/mem.o: $(WASMMOD_SRC)/pymergetic/util/mem/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/tlsf.o: $(WASMMOD)/third_party/tlsf/tlsf.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/dt.o: $(METAL_SRC)/pymergetic/metal/dt/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/pci.o: $(METAL_SRC)/pymergetic/metal/bus/pci/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/virtio_bus.o: $(METAL_SRC)/pymergetic/metal/bus/virtio/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/drivers.o: $(METAL_SRC)/pymergetic/metal/drivers/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/drivers_net.o: $(METAL_SRC)/pymergetic/metal/drivers/net/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/drivers_blk.o: $(METAL_SRC)/pymergetic/metal/drivers/blk/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/drivers_rtc.o: $(METAL_SRC)/pymergetic/metal/drivers/rtc/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/blk_virtio.o: $(METAL_SRC)/pymergetic/metal/drivers/blk/virtio/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/net_virtio.o: $(METAL_SRC)/pymergetic/metal/drivers/net/virtio/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/rtc_cmos.o: $(METAL_SRC)/pymergetic/metal/drivers/rtc/cmos/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/memmap.o: $(METAL_SRC)/pymergetic/metal/fw/memmap/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/async.o: $(METAL_SRC)/pymergetic/metal/async/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/ip.o: $(METAL_SRC)/pymergetic/metal/net/ip/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/modboot.o: $(WASMMOD_SRC)/pymergetic/wasmmod/boot/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/esp/EFI/BOOT/BOOTX64.EFI: $(FW_OBJS) $(FW_WAMR_LIBS) | $(BUILD)
	$(LLD_LINK) /subsystem:efi_application /entry:efi_main /nodefaultlib \
		/opt:ref /base:0x1000000 /out:$@ $(FW_OBJS) $(FW_WAMR_LIBS)

$(BUILD)/cake.img: | $(BUILD)
	dd if=/dev/zero of=$@ bs=512 count=32 status=none
	printf 'CAKE' | dd of=$@ conv=notrunc status=none

run: $(BUILD)/esp.img $(BUILD)/cake.img
	$(QEMU) $(QEMU_MACHINE) -serial file:$(BUILD)/serial.log -monitor none; \
	st=$$?; cat $(BUILD)/serial.log; \
	grep -q "upy metal ready" $(BUILD)/serial.log || exit 1; \
	grep -q "upy cdn" $(BUILD)/serial.log || exit 1; \
	grep -q "upy pack import" $(BUILD)/serial.log || exit 1; \
	if [ $$st -eq 1 ] || [ $$st -eq 0 ]; then exit 0; fi; exit $$st

clean:
	rm -rf $(BUILD)
