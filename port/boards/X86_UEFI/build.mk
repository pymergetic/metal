# IA32 UEFI seat: same PE as x86_64 until a 32-bit EFI toolchain is wired.
# CDN wants BOOTIA32.EFI next to the 64-bit image.
include boards/X86_64_UEFI/build.mk

all: $(BUILD)/esp/EFI/BOOT/BOOTIA32.EFI

$(BUILD)/esp/EFI/BOOT/BOOTIA32.EFI: $(BUILD)/esp/EFI/BOOT/BOOTX64.EFI
	mkdir -p $(dir $@)
	cp -f $< $@
