# X86_64_UEFI — PE EFI app via clang COFF + lld-link (docker if host lacks lld).
# µPy on UEFI needs mingw/windows headers + lld-link — next after BIOS REPL.
BOARD_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
BUILD ?= build-X86_64_UEFI
QEMU ?= qemu-system-x86_64
OVMF ?= /usr/share/ovmf/OVMF.fd
EDK_INC ?= $(abspath $(BOARD_DIR)/../../../_tmp/external/edk2/MdePkg/Include)
CLANG ?= clang

.PHONY: all run clean

all: $(BUILD)/esp/EFI/BOOT/BOOTX64.EFI

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/main.o: $(BOARD_DIR)/main.c | $(BUILD)
	$(CLANG) --target=x86_64-unknown-windows -ffreestanding -fno-stack-protector \
		-fshort-wchar -mno-red-zone -Wall -Wextra -O2 \
		-I$(EDK_INC) -I$(EDK_INC)/X64 \
		-c -o $@ $<

$(BUILD)/BOOTX64.EFI: $(BUILD)/main.o | $(BUILD)
	@if command -v lld-link >/dev/null 2>&1; then \
	  lld-link -subsystem:efi_application -entry:UefiMain -out:$@ $<; \
	elif ls /usr/lib/llvm-*/bin/lld-link >/dev/null 2>&1; then \
	  /usr/lib/llvm-*/bin/lld-link -subsystem:efi_application -entry:UefiMain -out:$@ $<; \
	else \
	  echo "note: host has no lld-link — linking EFI via docker"; \
	  docker run --rm -v $(abspath $(BUILD)):/b -w /b ubuntu:24.04 bash -lc '\
	    set -euo pipefail; \
	    export DEBIAN_FRONTEND=noninteractive; \
	    apt-get update -qq; \
	    apt-get install -y -qq lld clang >/tmp/apt.log; \
	    lld-link -subsystem:efi_application -entry:UefiMain -out:BOOTX64.EFI main.o'; \
	fi

$(BUILD)/esp/EFI/BOOT/BOOTX64.EFI: $(BUILD)/BOOTX64.EFI
	mkdir -p $(BUILD)/esp/EFI/BOOT
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
	for i in $$(seq 1 80); do \
	  if grep -a -q "ovmf ok" $(BUILD)/serial.log 2>/dev/null; then ok=1; break; fi; \
	  if ! kill -0 $$qpid 2>/dev/null; then break; fi; \
	  sleep 0.1; \
	done; \
	kill -KILL $$qpid 2>/dev/null; wait $$qpid 2>/dev/null; \
	echo "----- serial (trimmed) -----"; \
	grep -a -E "metal |ovmf ok|BdsDxe: (loading|starting) Boot0001" $(BUILD)/serial.log 2>/dev/null || true; \
	if [ $$ok -eq 1 ]; then echo "X86_64_UEFI_QEMU_OK"; exit 0; fi; \
	echo "X86_64_UEFI_QEMU_FAIL"; \
	tail -c 800 $(BUILD)/serial.log 2>/dev/null || true; \
	exit 1

clean:
	rm -rf $(BUILD)
