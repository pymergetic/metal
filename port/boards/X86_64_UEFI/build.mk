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
	-DPM_METAL_FIRMWARE=1 -DPM_WASMMOD_GUEST=0 -DPM_WASMMOD_IO_FILE=0 -DPM_METAL_UEFI=1 \
	-MMD -MP
ifeq ($(REPL),1)
CFLAGS_METAL += -DPM_METAL_UART_REPL=1
endif
INC := -I$(PORT_DIR)/fwinc -I$(PORT_DIR) -I$(BIOS_DIR) -I$(PORT_DIR)/bringup -I$(METAL_SRC) -I$(WASMMOD_SRC) -I$(WASMMOD) \
	-I$(abspath $(PORT_DIR)/../../..)

$(BUILD)/esp.img: $(BUILD)/esp/EFI/BOOT/BOOTX64.EFI
# ESP sized from the payload, not a fixed count: the EFI binary crossed
# 8MB (mcopy "Disk full" with a hardcoded count=8), so take its size,
# add FAT overhead + slack, and round up to whole MB.
	$(eval ESP_MB := $(shell stat -c%s $< | awk '{print int(($$1 + 3*1024*1024 - 1)/(1024*1024))}'))
	dd if=/dev/zero of=$@ bs=1M count=$(ESP_MB) status=none
	mkfs.vfat -n ESP $@ >/dev/null
	mmd -i $@ ::EFI ::EFI/BOOT
	mcopy -i $@ $< ::EFI/BOOT/BOOTX64.EFI

QEMU_MACHINE := -machine q35,accel=kvm:tcg -cpu max,-svm -m 256 -smp 4 \
	-vga none -audio none -display none \
	-bios $(OVMF) \
	-device isa-debug-exit,iobase=0x501,iosize=0x02 \
	-netdev user,id=n0 -device virtio-net-pci,disable-legacy=on,netdev=n0 \
	-netdev user,id=n1 -device virtio-net-pci,disable-legacy=on,netdev=n1 \
	-drive file=$(BUILD)/blk.img,if=none,format=raw,id=d0 \
	-device virtio-blk-pci,disable-legacy=on,drive=d0 \
	-drive file=$(BUILD)/esp.img,if=none,format=raw,id=esp \
	-device virtio-blk-pci,disable-legacy=on,drive=esp,bootindex=0 \
	-device virtio-gpu-pci,disable-legacy=on \
	-device virtio-keyboard-pci,disable-legacy=on \
	-device virtio-tablet-pci,disable-legacy=on

# Interactive run seat: same machine as prove but the n0 user-net also hostfwds
# host 0.0.0.0 -> guest (10.0.2.15 DHCP) so the auto-served httpd/sshd are
# reachable from the host and from any host on the LAN (ssh -p2222 <host-ip>).
QEMU_RUN_MACHINE := -machine q35,accel=kvm:tcg -cpu max,-svm -m 256 -smp 4 \
	-vga none -audio none -display none \
	-bios $(OVMF) \
	-device isa-debug-exit,iobase=0x501,iosize=0x02 \
	-netdev user,id=n0,hostfwd=tcp:0.0.0.0:8090-10.0.2.15:8090,hostfwd=tcp:0.0.0.0:2222-10.0.2.15:2222 \
	-device virtio-net-pci,disable-legacy=on,netdev=n0 \
	-netdev user,id=n1 -device virtio-net-pci,disable-legacy=on,netdev=n1 \
	-drive file=$(BUILD)/blk.img,if=none,format=raw,id=d0 \
	-device virtio-blk-pci,disable-legacy=on,drive=d0 \
	-drive file=$(BUILD)/esp.img,if=none,format=raw,id=esp \
	-device virtio-blk-pci,disable-legacy=on,drive=esp,bootindex=0 \
	-device virtio-gpu-pci,disable-legacy=on \
	-device virtio-keyboard-pci,disable-legacy=on \
	-device virtio-tablet-pci,disable-legacy=on

FW_OBJS := \
	$(BUILD)/crt.o \
	$(BUILD)/uart.o \
	$(BUILD)/main.o \
	$(BUILD)/lib.o \
	$(BUILD)/mem.o \
	$(BUILD)/limits.o \
	$(BUILD)/nativecall_card.o \
	$(BUILD)/types.o \
	$(BUILD)/tlsf.o \
	$(BUILD)/smp.o \
	$(BUILD)/smp_tramp.o \
	$(BUILD)/modboot.o
# Cards are added by fw_cards.mk from the card tree — do not list them here.

FW_RUSTC_TARGET := x86_64-unknown-uefi
FW_WAMR_UEFI := 1
include $(PORT_DIR)/fw_cdn.mk
include $(PORT_DIR)/fw_mbedtls.mk
include $(PORT_DIR)/fw_zenoh.mk
# In-kernel compile face: the vendored TCC, native x86_64 plus the cross
# lanes (the every-seat-builds-every-arch matrix: this seat emits wasm32
# and arm objects too). Before fw_cards.mk so jit.c compiles with
# PM_HAS_TCC=1, the LIB_DIR, and the PM_METAL_TCC_CROSS_* gates.
# The PE toolchain seam:
# fw_tcc.mk's instance recipe compiles with TCC_CFLAGS, so lld-link gets a
# COFF libtcc.o, not an ELF it refuses ("unknown file type").
# -U_WIN32 -U_WIN64: the windows triplet defines _WIN32/_WIN64, which send
# tcc.h into its windows.h/io.h/direct.h block (no such headers freestanding)
# and tccrun.c into its Win64 unwind-registration path (RtlAddFunctionTable,
# pe_imagebase — a PE loader feature UEFI-side TCC never uses; the object
# path emits ELF ET_REL). Keep the Win64 ABI from the triple, drop the
# macros: TCC compiles the same POSIX shape against fwinc as the BIOS seat
# and emits the same ELF ET_REL objects — one object format, one loader
# (load.c), every seat.
TCC_CFLAGS := --target=x86_64-unknown-windows -ffreestanding -fno-stack-protector -mno-red-zone -U_WIN32 -U_WIN64 -Wno-incompatible-library-redeclaration
FW_TCC_CROSS := wasm32 arm
include $(PORT_DIR)/fw_tcc.mk
include $(PORT_DIR)/fw_cards.mk
include $(PORT_DIR)/fw_wamr.mk
include $(PORT_DIR)/upy.mk

# The live half of the prove: a pack server on the host loopback, which the
# guest reaches at the user-net gateway. Port agreed with upy/firmware_upy_cdn.py.
CDN_PORT ?= 18124
CDN_PACKS := $(WASMMOD)/examples/packs
LIVE_CDN := $(PORT_DIR)/live_cdn.sh $(CDN_PACKS) $(CDN_PORT) $(BUILD)/cdn.log

.PHONY: all prove run upload clean
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

$(BUILD)/limits.o: $(WASMMOD_SRC)/pymergetic/util/limits/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

# pymergetic.wasmmod.nativecall — the rooms the µPy bridge's calls work in. The
# glue (ports/micropython/nativecall.c) names its faces, so a board that carries
# the glue carries the card.
$(BUILD)/nativecall_card.o: $(WASMMOD_SRC)/pymergetic/wasmmod/nativecall/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/types.o: $(WASMMOD_SRC)/pymergetic/types/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/tlsf.o: $(WASMMOD)/third_party/tlsf/tlsf.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/smp.o: $(PORT_DIR)/smp/smp_x86.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/smp_tramp.o: $(PORT_DIR)/smp/trampoline.S | $(BUILD)
	$(CC) $(CFLAGS_METAL) -c -o $@ $<

$(BUILD)/modboot.o: $(WASMMOD_SRC)/pymergetic/wasmmod/boot/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/esp/EFI/BOOT/BOOTX64.EFI: $(FW_OBJS) $(FW_WAMR_LIBS) | $(BUILD)
	$(LLD_LINK) /subsystem:efi_application /entry:efi_main /nodefaultlib \
		/opt:ref /base:0x1000000 /out:$@ $(FW_OBJS) $(FW_WAMR_LIBS)

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

prove: $(BUILD)/esp.img $(BUILD)/blk.img
	$(LIVE_CDN) $(PORT_DIR)/serial_prove.sh $(BUILD)/serial.log $(BOARD_DIR)/prove_markers.txt \
		$(QEMU) $(QEMU_MACHINE) -serial file:$(BUILD)/serial.log -monitor none

run: $(BUILD)/esp.img $(BUILD)/blk.img
	$(QEMU) $(QEMU_RUN_MACHINE) -serial mon:stdio; \
	st=$$?; if [ $$st -eq 1 ] || [ $$st -eq 0 ]; then exit 0; fi; exit $$st

upload: $(BUILD)/esp/EFI/BOOT/BOOTX64.EFI
	$(PORT_DIR)/upload.sh uefi $(BUILD)

# Header dependencies. Without these a card's __types__.h can change the shape
# of a ring and the objects around it are not rebuilt, so the image is built
# from two different opinions of that shape. The .d files sit beside the .o
# they describe and only exist for objects that have been built, so whatever is
# there is the whole list — cards, the board's own objects, mbedtls, zenoh. (µPy
# brings its own: mkrules.mk reads a .P per object.)
-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
