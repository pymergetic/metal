# X86_64_BIOS — freestanding Multiboot trampoline + COM1 banner (no forge).
#
#   make BOARD=X86_64_BIOS
#   make BOARD=X86_64_BIOS run

BOARD_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
BUILD ?= build-X86_64_BIOS
QEMU ?= qemu-system-x86_64
CLANG ?= clang
LD ?= ld
OBJCOPY ?= objcopy

CFLAGS64 := -m64 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-mno-red-zone -Wall -Wextra -O2
ASFLAGS64 := -m64
CFLAGS32 := -m32 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-Wall -Wextra -O2
ASFLAGS32 := -m32

.PHONY: all run clean

all: $(BUILD)/metal.qemu.elf

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/crt0.o: $(BOARD_DIR)/crt0.S | $(BUILD)
	$(CLANG) $(ASFLAGS64) -c -o $@ $<

$(BUILD)/main.o: $(BOARD_DIR)/main.c $(BOARD_DIR)/io.h | $(BUILD)
	$(CLANG) $(CFLAGS64) -I$(BOARD_DIR) -c -o $@ $<

$(BUILD)/uart.o: $(BOARD_DIR)/uart.c $(BOARD_DIR)/io.h | $(BUILD)
	$(CLANG) $(CFLAGS64) -I$(BOARD_DIR) -c -o $@ $<

$(BUILD)/metal.elf: $(BUILD)/crt0.o $(BUILD)/main.o $(BUILD)/uart.o $(BOARD_DIR)/link.ld
	$(LD) -m elf_x86_64 -nostdlib -T $(BOARD_DIR)/link.ld -o $@ \
		$(BUILD)/crt0.o $(BUILD)/main.o $(BUILD)/uart.o

# Embed ELF64 as binary blob → _binary_metal_elf_{start,end}
$(BUILD)/metal_elf.o: $(BUILD)/metal.elf | $(BUILD)
	cd $(BUILD) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 metal.elf metal_elf.o

$(BUILD)/trampoline32.o: $(BOARD_DIR)/trampoline32.S | $(BUILD)
	$(CLANG) $(ASFLAGS32) -c -o $@ $<

$(BUILD)/trampoline_load.o: $(BOARD_DIR)/trampoline_load.c | $(BUILD)
	$(CLANG) $(CFLAGS32) -c -o $@ $<

$(BUILD)/trampoline64.o64: $(BOARD_DIR)/trampoline64.S | $(BUILD)
	$(CLANG) $(ASFLAGS64) -c -o $@ $<

$(BUILD)/trampoline64.o: $(BUILD)/trampoline64.o64 | $(BUILD)
	$(OBJCOPY) -O elf32-i386 $< $@

$(BUILD)/metal.qemu.elf: $(BUILD)/trampoline32.o $(BUILD)/trampoline64.o \
		$(BUILD)/trampoline_load.o $(BUILD)/metal_elf.o $(BOARD_DIR)/link32.ld
	$(LD) -m elf_i386 -nostdlib -T $(BOARD_DIR)/link32.ld -o $@ \
		$(BUILD)/trampoline32.o $(BUILD)/trampoline64.o \
		$(BUILD)/trampoline_load.o $(BUILD)/metal_elf.o

# isa-debug-exit: guest outw(0x501,0) → qemu exit status 1 (success for us).
run: $(BUILD)/metal.qemu.elf
	@set +e; \
	$(QEMU) -machine q35,accel=kvm:tcg -m 256 -vga none \
		-device isa-debug-exit,iobase=0x501,iosize=0x02 \
		-display none -serial file:$(BUILD)/serial.log \
		-kernel $(BUILD)/metal.qemu.elf; \
	ec=$$?; \
	echo "----- serial -----"; \
	cat $(BUILD)/serial.log; \
	if grep -q "qemu ok" $(BUILD)/serial.log; then \
	  echo "X86_64_BIOS_QEMU_OK"; \
	  exit 0; \
	fi; \
	echo "X86_64_BIOS_QEMU_FAIL (qemu ec=$$ec)"; \
	exit 1

clean:
	rm -rf $(BUILD)
