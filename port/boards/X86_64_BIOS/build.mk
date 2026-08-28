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
	-DPM_METAL_FIRMWARE=1 -DPM_WASMMOD_GUEST=0 -DPM_WASMMOD_IO_FILE=0
ifeq ($(REPL),1)
CFLAGS_METAL += -DPM_METAL_UART_REPL=1
endif
CFLAGS32 := -m32 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -Wall -O2
ASFLAGS64 := -m64
ASFLAGS32 := -m32
INC := -I$(PORT_DIR)/fwinc -I$(BOARD_DIR) -I$(PORT_DIR) -I$(PORT_DIR)/bringup -I$(METAL_SRC) -I$(WASMMOD_SRC) -I$(WASMMOD) \
	-I$(abspath $(PORT_DIR)/../../..)

QEMU_MACHINE := -machine q35,accel=kvm:tcg -cpu max,-svm -m 256 -smp 4 \
	-vga none -audio none -display none \
	-device isa-debug-exit,iobase=0x501,iosize=0x02 \
	-netdev user,id=n0 -device virtio-net-pci,disable-legacy=on,netdev=n0 \
	-netdev user,id=n1 -device virtio-net-pci,disable-legacy=on,netdev=n1 \
	-drive file=$(BUILD)/blk.img,if=none,format=raw,id=d0 \
	-device virtio-blk-pci,disable-legacy=on,drive=d0 \
	-device virtio-gpu-pci,disable-legacy=on \
	-device virtio-keyboard-pci,disable-legacy=on \
	-device virtio-tablet-pci,disable-legacy=on

FW_OBJS := \
	$(BUILD)/crt0.o \
	$(BUILD)/elf_ctors.o \
	$(BUILD)/uart.o \
	$(BUILD)/main.o \
	$(BUILD)/lib.o \
	$(BUILD)/mem.o \
	$(BUILD)/tlsf.o \
	$(BUILD)/smp.o \
	$(BUILD)/smp_tramp.o \
	$(BUILD)/modboot.o
# Cards are added by fw_cards.mk from the card tree — do not list them here.

FW_RUSTC_TARGET := x86_64-unknown-none
include $(PORT_DIR)/fw_cdn.mk
include $(PORT_DIR)/fw_mbedtls.mk
include $(PORT_DIR)/fw_zenoh.mk
include $(PORT_DIR)/fw_cards.mk
include $(PORT_DIR)/fw_wamr.mk
include $(PORT_DIR)/upy.mk

# The live half of the prove: a pack server on the host loopback, which the
# guest reaches at the user-net gateway. Port agreed with upy/firmware_upy_cdn.py.
CDN_PORT ?= 18124
CDN_PACKS := $(WASMMOD)/examples/packs
LIVE_CDN := $(PORT_DIR)/live_cdn.sh $(CDN_PACKS) $(CDN_PORT) $(BUILD)/cdn.log

.PHONY: all prove run upload clean
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

$(BUILD)/smp.o: $(PORT_DIR)/smp/smp_x86.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/smp_tramp.o: $(PORT_DIR)/smp/trampoline.S | $(BUILD)
	$(CC) $(ASFLAGS64) -c -o $@ $<

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

$(BUILD)/blk.img: | $(BUILD)
	dd if=/dev/zero of=$@ bs=512 count=512 status=none
	mkfs.vfat -F 12 -n METAL $@ >/dev/null
	# prove_x86() in main.c reads LBA0 and requires a "METL" brand. mkfs puts
	# "METAL" only in the BPB OEM field, so stamp the first 4 bytes again.
	printf 'METL' | dd of=$@ conv=notrunc status=none
	printf 'hi\n' > $(BUILD)/hi.txt
	printf 'x' > $(BUILD)/x.txt
	printf 'ok\n' > $(BUILD)/hello_fat.txt
	mmd -i $@ ::SUB
	mcopy -i $@ $(BUILD)/hi.txt ::HI.TXT
	mcopy -i $@ $(BUILD)/x.txt ::SUB/X.TXT
	mcopy -i $@ $(BUILD)/hello_fat.txt ::hello.txt

prove: $(BUILD)/metal.qemu.elf $(BUILD)/blk.img
	$(LIVE_CDN) $(QEMU) $(QEMU_MACHINE) -serial file:$(BUILD)/serial.log -monitor none -kernel $(BUILD)/metal.qemu.elf; \
	st=$$?; cat $(BUILD)/serial.log; \
	grep -q "pymergetic metal" $(BUILD)/serial.log || exit 1; \
	grep -q "\`-- ready" $(BUILD)/serial.log || exit 1; \
	grep -q "smp" $(BUILD)/serial.log || exit 1; \
	grep -q "4 runner" $(BUILD)/serial.log || exit 1; \
	grep -q "externals" $(BUILD)/serial.log || exit 1; \
	grep -q "lz4" $(BUILD)/serial.log || exit 1; \
	grep -q "mtar" $(BUILD)/serial.log || exit 1; \
	grep -q "packages()" $(BUILD)/serial.log || exit 1; \
	grep -q "run m.net.ssh.listen(0x0, 2222)" $(BUILD)/serial.log || exit 1; \
	grep -q "viewport" $(BUILD)/serial.log || exit 1; \
	grep -q "upy metal ready" $(BUILD)/serial.log || exit 1; \
	grep -q "upy native card import" $(BUILD)/serial.log || exit 1; \
	grep -q "upy inspect" $(BUILD)/serial.log || exit 1; \
	grep -q "upy inspect caps" $(BUILD)/serial.log || exit 1; \
	grep -q "upy changes ledger" $(BUILD)/serial.log || exit 1; \
	grep -q "upy dns" $(BUILD)/serial.log || exit 1; \
	grep -q "upy socket" $(BUILD)/serial.log || exit 1; \
	grep -q "upy swarm" $(BUILD)/serial.log || exit 1; \
	grep -q "upy cdn" $(BUILD)/serial.log || exit 1; \
	grep -q "upy pack import" $(BUILD)/serial.log || exit 1; \
	grep -q "dhcp" $(BUILD)/serial.log || exit 1; \
	grep -q "10.0.2.15" $(BUILD)/serial.log || exit 1; \
	grep -q "10.0.2.2" $(BUILD)/serial.log || exit 1; \
	grep -q "upy cdn fetch 11" $(BUILD)/serial.log || exit 1; \
	grep -q "upy display present" $(BUILD)/serial.log || exit 1; \
	grep -q "upy input feed" $(BUILD)/serial.log || exit 1; \
	grep -q "upy console ids" $(BUILD)/serial.log || exit 1; \
	grep -q "upy fs embed" $(BUILD)/serial.log || exit 1; \
	grep -q "upy process" $(BUILD)/serial.log || exit 1; \
	grep -q "upy ssh session" $(BUILD)/serial.log || exit 1; \
	grep -q "upy zenoh" $(BUILD)/serial.log || exit 1; \
	if [ $$st -eq 1 ] || [ $$st -eq 0 ]; then exit 0; fi; exit $$st

# Interactive run seat: same machine as prove but the n0 user-net also hostfwds
# host 0.0.0.0 -> guest (10.0.2.15 DHCP) so the auto-served httpd/sshd are
# reachable from the host and from any host on the LAN (ssh -p2222 <host-ip>).
QEMU_RUN_MACHINE := -machine q35,accel=kvm:tcg -cpu max,-svm -m 256 -smp 4 \
	-vga none -audio none -display none -device isa-debug-exit,iobase=0x501,iosize=0x02 \
	-netdev user,id=n0,hostfwd=tcp:0.0.0.0:8090-10.0.2.15:8090,hostfwd=tcp:0.0.0.0:2222-10.0.2.15:2222 \
	-device virtio-net-pci,disable-legacy=on,netdev=n0 \
	-netdev user,id=n1 -device virtio-net-pci,disable-legacy=on,netdev=n1 \
	-drive file=$(BUILD)/blk.img,if=none,format=raw,id=d0 \
	-device virtio-blk-pci,disable-legacy=on,drive=d0 \
	-device virtio-gpu-pci,disable-legacy=on \
	-device virtio-keyboard-pci,disable-legacy=on \
	-device virtio-tablet-pci,disable-legacy=on

run: $(BUILD)/metal.qemu.elf $(BUILD)/blk.img
	$(QEMU) $(QEMU_RUN_MACHINE) -serial mon:stdio -kernel $(BUILD)/metal.qemu.elf; \
	st=$$?; if [ $$st -eq 1 ] || [ $$st -eq 0 ]; then exit 0; fi; exit $$st

upload: $(BUILD)/metal.qemu.elf
	$(PORT_DIR)/upload.sh bios $(BUILD)
