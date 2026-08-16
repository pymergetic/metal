# X86_64_BIOS — Multiboot trampoline + live metal driver core (dual virtio-net + blk).
#
#   make -C extmod/metal/port BOARD=X86_64_BIOS ENGINE=mp REPL=1

BOARD_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
PORT_DIR := $(CURDIR)
METAL_DIR := $(abspath $(PORT_DIR)/..)
WASMMOD := $(abspath $(PORT_DIR)/../../wasmmod)
METAL_SRC := $(METAL_DIR)/src
WASMMOD_SRC := $(WASMMOD)/src
BUILD ?= build/X86_64_BIOS-mp-repl
CLANG ?= clang
CC := $(CLANG)
LD ?= ld
OBJCOPY ?= objcopy
QEMU ?= qemu-system-x86_64
RUSTC ?= rustc

CFLAGS_METAL := -m64 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-mno-red-zone -fno-asynchronous-unwind-tables -fno-exceptions \
	-Wall -Wextra -Wno-unused-parameter -Os -DNDEBUG -std=gnu99 \
	-DPM_METAL_FIRMWARE=1 -DPM_WASMMOD_GUEST=1
CFLAGS32 := -m32 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -Wall -O2
ASFLAGS64 := -m64
ASFLAGS32 := -m32
INC := -I$(PORT_DIR)/fwinc -I$(BOARD_DIR) -I$(PORT_DIR) -I$(PORT_DIR)/bringup -I$(METAL_SRC) -I$(WASMMOD_SRC) -I$(WASMMOD) \
	-I$(abspath $(PORT_DIR)/../../..)

QEMU_MACHINE := -machine q35,accel=kvm:tcg -cpu max,-svm -m 128 -smp 1 \
	-vga none -audio none -display none \
	-device isa-debug-exit,iobase=0x501,iosize=0x02 \
	-netdev user,id=n0 -device virtio-net-pci,disable-legacy=on,netdev=n0 \
	-netdev user,id=n1 -device virtio-net-pci,disable-legacy=on,netdev=n1 \
	-drive file=$(BUILD)/cake.img,if=none,format=raw,id=d0 \
	-device virtio-blk-pci,disable-legacy=on,drive=d0

FW_OBJS := \
	$(BUILD)/crt0.o \
	$(BUILD)/elf_ctors.o \
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

FW_RUSTC_TARGET := x86_64-unknown-none
include $(PORT_DIR)/fw_cdn.mk
include $(PORT_DIR)/fw_wamr.mk
include $(PORT_DIR)/upy.mk

.PHONY: all run clean
all: $(BUILD)/metal.qemu.elf

$(BUILD):
	mkdir -p $@

$(BUILD)/crt0.o: $(BOARD_DIR)/crt0.S | $(BUILD)
	$(CC) $(ASFLAGS64) -c -o $@ $<

$(BUILD)/elf_ctors.o: $(PORT_DIR)/elf_ctors.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) -c -o $@ $<

$(BUILD)/uart.o: $(BOARD_DIR)/uart.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/main.o: $(BOARD_DIR)/main.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/lib.o: $(PORT_DIR)/lib.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) -c -o $@ $<

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

LIBGCC := $(shell $(CC) $(CFLAGS_METAL) -print-libgcc-file-name)

$(BUILD)/metal.elf: $(FW_OBJS) $(FW_WAMR_LIBS) $(BOARD_DIR)/link.ld
	$(LD) -m elf_x86_64 -nostdlib -z noexecstack -T $(BOARD_DIR)/link.ld \
		--gc-sections -o $@ $(FW_OBJS) $(FW_WAMR_LIBS) $(LIBGCC)

$(BUILD)/gnustack32.o: | $(BUILD)
	printf '.section .note.GNU-stack,"",@progbits\n' | \
		$(CC) $(ASFLAGS32) -c -x assembler -o $@ -

$(BUILD)/metal_elf.o: $(BUILD)/metal.elf $(BUILD)/gnustack32.o | $(BUILD)
	cd $(BUILD) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 metal.elf metal_elf.raw.o
	$(LD) -m elf_i386 -r -z noexecstack -o $@ $(BUILD)/metal_elf.raw.o $(BUILD)/gnustack32.o

$(BUILD)/trampoline32.o: $(BOARD_DIR)/trampoline32.S | $(BUILD)
	$(CC) $(ASFLAGS32) -c -o $@ $<

$(BUILD)/trampoline_load.o: $(BOARD_DIR)/trampoline_load.c | $(BUILD)
	$(CC) $(CFLAGS32) -c -o $@ $<

$(BUILD)/trampoline64.o64: $(BOARD_DIR)/trampoline64.S | $(BUILD)
	$(CC) $(ASFLAGS64) -c -o $@ $<

$(BUILD)/trampoline64.o: $(BUILD)/trampoline64.o64 | $(BUILD)
	$(OBJCOPY) -O elf32-i386 $< $@

$(BUILD)/metal.qemu.elf: $(BUILD)/trampoline32.o $(BUILD)/trampoline64.o \
		$(BUILD)/trampoline_load.o $(BUILD)/metal_elf.o $(BOARD_DIR)/link32.ld
	$(LD) -m elf_i386 -nostdlib -z noexecstack -T $(BOARD_DIR)/link32.ld -o $@ \
		$(BUILD)/trampoline32.o $(BUILD)/trampoline64.o \
		$(BUILD)/trampoline_load.o $(BUILD)/metal_elf.o

$(BUILD)/cake.img: | $(BUILD)
	dd if=/dev/zero of=$@ bs=512 count=32 status=none
	printf 'CAKE' | dd of=$@ conv=notrunc status=none

run: $(BUILD)/metal.qemu.elf $(BUILD)/cake.img
	$(QEMU) $(QEMU_MACHINE) -serial file:$(BUILD)/serial.log -monitor none -kernel $(BUILD)/metal.qemu.elf; \
	st=$$?; cat $(BUILD)/serial.log; \
	grep -q "upy metal ready" $(BUILD)/serial.log || exit 1; \
	grep -q "upy cdn" $(BUILD)/serial.log || exit 1; \
	grep -q "upy pack import" $(BUILD)/serial.log || exit 1; \
	if [ $$st -eq 1 ] || [ $$st -eq 0 ]; then exit 0; fi; exit $$st

clean:
	rm -rf $(BUILD)
