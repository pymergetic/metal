# Luckfox Pico Max — Rockchip RV1106 Cortex-A7 (armv7l).
#
#   make -C extmod/metal/port BOARD=ARMV7_RV1106
# Produces $(BUILD)/metal.bin at 0x8000 (Rockchip kernel_addr_r / boot_fit).
# Pack/flash extras need RKBIN + RKTOOLS (rockchip rkbin + rkdeveloptool/mkimage).
# Board address is LUCKFOX_IP (required only for `run`).

BOARD_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
PORT_DIR := $(CURDIR)
METAL_DIR := $(abspath $(PORT_DIR)/..)
WASMMOD := $(abspath $(PORT_DIR)/../../wasmmod)
METAL_SRC := $(METAL_DIR)/src
WASMMOD_SRC := $(WASMMOD)/src
BUILD ?= build/ARMV7_RV1106-mp-repl
CLANG ?= clang
CC := $(CLANG)
LD ?= ld.lld
OBJCOPY ?= llvm-objcopy-18
RUSTC ?= rustc
CLANG_TARGET := armv7-none-eabihf
LIBCLANG_RT ?=
RKBIN ?=
RKTOOLS ?=
MKIMAGE :=
BOOT_MERGER :=
RK_DDR :=
RK_SPL :=
ifneq ($(RKTOOLS),)
MKIMAGE := $(firstword $(wildcard $(RKTOOLS)/root/usr/bin/mkimage) $(wildcard $(RKTOOLS)/usr/bin/mkimage) $(wildcard $(RKTOOLS)/mkimage))
endif
ifneq ($(RKBIN),)
BOOT_MERGER := $(wildcard $(RKBIN)/tools/boot_merger)
RK_DDR := $(wildcard $(RKBIN)/bin/rv11/rv1106_ddr_924MHz_v1.15.bin)
RK_SPL := $(wildcard $(RKBIN)/bin/rv11/rv1106_spl_v1.02.bin)
endif
PACK_EXTRAS :=
ifneq ($(MKIMAGE),)
PACK_EXTRAS += $(BUILD)/metal.itb
endif
ifneq ($(BOOT_MERGER),)
ifneq ($(RK_DDR),)
ifneq ($(RK_SPL),)
PACK_EXTRAS += $(BUILD)/rv1106_download.bin
endif
endif
endif

CFLAGS_METAL := --target=$(CLANG_TARGET) -marm -mfpu=neon-vfpv4 -mfloat-abi=hard \
	-ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-fno-asynchronous-unwind-tables -fno-exceptions \
	-Wall -Wextra -Wno-unused-parameter -Os -DNDEBUG -std=gnu99 \
	-DPM_METAL_FIRMWARE=1 -DPM_WASMMOD_GUEST=0 -DPM_WASMMOD_IO_FILE=0 \
	-DPM_METAL_UART_REPL=1 \
	-DMICROPY_NLR_SETJMP=1 \
	-DMICROPY_HW_MCU_NAME='"rv1106"'
INC := -I$(PORT_DIR)/fwinc -I$(BOARD_DIR) -I$(PORT_DIR) -I$(PORT_DIR)/bringup -I$(METAL_SRC) -I$(WASMMOD_SRC) -I$(WASMMOD) \
	-I$(abspath $(PORT_DIR)/../../..)

FW_OBJS := \
	$(BUILD)/crt0.o \
	$(BUILD)/elf_ctors.o \
	$(BUILD)/uart.o \
	$(BUILD)/led.o \
	$(BUILD)/main.o \
	$(BUILD)/lib.o \
	$(BUILD)/mem.o \
	$(BUILD)/tlsf.o \
	$(BUILD)/modboot.o
# Cards are added by fw_cards.mk from the card tree — do not list them here.

FW_RUSTC_TARGET := armv7a-none-eabihf
FW_WAMR_ARCH := armv7
include $(PORT_DIR)/fw_cdn.mk
include $(PORT_DIR)/fw_mbedtls.mk
include $(PORT_DIR)/fw_cards.mk
include $(PORT_DIR)/fw_wamr.mk
include $(PORT_DIR)/upy.mk

.PHONY: all prove pack run upload clean
all: $(BUILD)/metal.bin $(PACK_EXTRAS)
prove: $(BUILD)/metal.bin

$(BUILD):
	mkdir -p $@

$(BUILD)/crt0.o: $(BOARD_DIR)/crt0.S | $(BUILD)
	$(CC) $(CFLAGS_METAL) -c -o $@ $<

$(BUILD)/elf_ctors.o: $(PORT_DIR)/elf_ctors.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) -c -o $@ $<

$(BUILD)/uart.o: $(BOARD_DIR)/uart.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/led.o: $(BOARD_DIR)/led.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/main.o: $(BOARD_DIR)/main.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/lib.o: $(PORT_DIR)/lib.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/mem.o: $(WASMMOD_SRC)/pymergetic/util/mem/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/tlsf.o: $(WASMMOD)/third_party/tlsf/tlsf.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/modboot.o: $(WASMMOD_SRC)/pymergetic/wasmmod/boot/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/metal.elf: $(FW_OBJS) $(FW_WAMR_LIBS) $(BOARD_DIR)/link.ld
	$(CC) $(CFLAGS_METAL) -nostdlib -fuse-ld=lld \
		-Wl,-T,$(BOARD_DIR)/link.ld -Wl,--gc-sections -Wl,-z,norelro \
		-o $@ $(FW_OBJS) $(FW_WAMR_LIBS)

$(BUILD)/metal.bin: $(BUILD)/metal.elf
	$(OBJCOPY) -O binary $< $@
	@echo "RV1106 image $@"
	@if [ -n "$(MKIMAGE)" ] && [ -x "$(MKIMAGE)" ]; then \
		$(MKIMAGE) -A arm -O linux -T kernel -C none \
			-a 0x00008000 -e 0x00008000 -n metal -d $@ $(BUILD)/metal.uimg; \
	fi

$(BUILD)/metal.itb: $(BUILD)/metal.bin $(BOARD_DIR)/metal.its $(BOARD_DIR)/rv1106.dtb $(BOARD_DIR)/resource.img
	@test -n "$(MKIMAGE)" && test -x "$(MKIMAGE)" || { echo "set RKTOOLS to a tree with mkimage"; exit 1; }
	cp $(BOARD_DIR)/rv1106.dtb $(BOARD_DIR)/metal.its $(BOARD_DIR)/resource.img $(BUILD)/
	cd $(BUILD) && $(MKIMAGE) -f metal.its -E -p 0x800 metal.itb

$(BUILD)/rk472.bin: $(BOARD_DIR)/rk472.S $(BUILD)/metal.bin
	$(CC) $(CFLAGS_METAL) -I$(abspath $(BUILD)) -c -o $(BUILD)/rk472.o $(BOARD_DIR)/rk472.S
	$(OBJCOPY) -O binary --only-section=.text $(BUILD)/rk472.o $(BUILD)/rk472.raw.bin
	python3 $(BOARD_DIR)/pack_rk472.py $(BUILD)/rk472.raw.bin $@

$(BUILD)/rv1106_download.bin: $(BUILD)/rk472.bin
	@test -n "$(BOOT_MERGER)" && test -n "$(RK_DDR)" && test -n "$(RK_SPL)" || { \
		echo "set RKBIN to a rockchip rkbin checkout (ddr + spl + tools/boot_merger)"; exit 1; }
	@mkdir -p $(BUILD)/rkboot/bin/rv11
	@cp $(RK_DDR) $(BUILD)/rkboot/bin/rv11/
	@cp $(RK_SPL) $(BUILD)/rkboot/bin/rv11/
	@cp $< $(BUILD)/rkboot/rk472.bin
	@printf '%s\n' \
		'[CHIP_NAME]' 'NAME=RV1106' \
		'[VERSION]' 'MAJOR=1' 'MINOR=1' \
		'[CODE471_OPTION]' 'NUM=1' 'Path1=bin/rv11/rv1106_ddr_924MHz_v1.15.bin' 'Sleep=1' \
		'[CODE472_OPTION]' 'NUM=1' 'Path1=rk472.bin' \
		'[LOADER_OPTION]' 'NUM=2' 'LOADER1=FlashData' 'LOADER2=FlashBoot' \
		'FlashData=bin/rv11/rv1106_ddr_924MHz_v1.15.bin' \
		'FlashBoot=bin/rv11/rv1106_spl_v1.02.bin' \
		'[OUTPUT]' 'PATH=rv1106_download.bin' \
		'[FLAG]' '471_RC4_OFF=true' 'RC4_OFF=true' \
		> $(BUILD)/rkboot/RV1106METAL.ini
	cd $(BUILD)/rkboot && $(BOOT_MERGER) RV1106METAL.ini
	cp $(BUILD)/rkboot/rv1106_download.bin $@

pack: $(BUILD)/metal.itb $(BUILD)/rv1106_download.bin

run: pack
	$(BOARD_DIR)/ram_boot.sh $(BUILD)/metal.bin

upload: run
