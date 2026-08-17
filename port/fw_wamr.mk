# Freestanding WAMR + RS loader/registry/version on BIOS and UEFI.
# Include after FW_OBJS is started. Link $(FW_WAMR_LIBS) with the image.
HELLO_WASM := $(WASMMOD)/examples/packs/pymergetic.wasmmod_examples.hello.wasm
WAMR_OUT := $(BUILD)/wamr
FW_WAMR_UEFI ?= 0
FW_WAMR_ARCH ?= x86_64
ifeq ($(FW_WAMR_ARCH),armv7)
FW_WAMR_BUILD_TARGET ?= -DBUILD_TARGET_ARM_VFP
FW_RUSTC_REDZONE :=
else
FW_WAMR_BUILD_TARGET ?= -DBUILD_TARGET_X86_64
FW_RUSTC_REDZONE := -C no-redzone=yes
endif

$(BUILD)/libwasmmod_wamr_freestanding.a: | $(BUILD)
	$(MAKE) -f $(WASMMOD)/ports/freestanding/wamr_freestanding.mk \
		OUT_DIR=$(WAMR_OUT) \
		ARCH=$(FW_WAMR_ARCH) \
		UEFI=$(FW_WAMR_UEFI) \
		PLAT_BH_PLATFORM=METAL \
		PLAT_INC=$(PORT_DIR)/wamr \
		PLAT_PORT_INC=$(PORT_DIR) \
		PLAT_LIBC_INC=$(PORT_DIR)/fwinc \
		PLAT_SRC_INC=$(METAL_SRC) \
		PLAT_EXTRA_INC=$(METAL_SRC) \
		WASMMOD_DIR=$(WASMMOD)
	cp $(WAMR_OUT)/libwasmmod_wamr_freestanding.a $@

$(BUILD)/metal_platform.o: $(PORT_DIR)/wamr/metal_platform.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -I$(PORT_DIR)/wamr \
		-I$(WASMMOD)/third_party/wamr/core/iwasm/include \
		-I$(WASMMOD)/third_party/wamr/core/shared/platform/include \
		-I$(WASMMOD)/third_party/wamr/core \
		-DBH_PLATFORM_METAL $(FW_WAMR_BUILD_TARGET) \
		-DWASM_ENABLE_INTERP=1 -DWASM_ENABLE_FAST_INTERP=1 \
		-DWASM_ENABLE_SHARED_HEAP=1 -DWASM_DISABLE_HW_BOUND_CHECK=1 \
		-c -o $@ $<

$(BUILD)/hello_pack.c: $(HELLO_WASM) $(PORT_DIR)/embed_bytes.py | $(BUILD)
	python3 $(PORT_DIR)/embed_bytes.py -o $@ \
		--bytes pm_metal_hello_wasm $(HELLO_WASM)

$(BUILD)/hello_pack.o: $(BUILD)/hello_pack.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) -c -o $@ $<

ifeq ($(FW_WAMR_UEFI),1)
FW_RUSTC_CPU := -C target-feature=-sse,-sse2 -C relocation-model=static
else
FW_RUSTC_CPU := -C relocation-model=static
endif
$(BUILD)/libfw_lock.a: $(PORT_DIR)/fw_lock/lib.rs \
		$(PORT_DIR)/fw_lock/util.rs \
		$(PORT_DIR)/fw_lock/wasmmod.rs \
		$(WASMMOD)/src/pymergetic/util/lock/__impl__.rs \
		$(WASMMOD)/src/pymergetic/util/lz4/__impl__.rs \
		$(WASMMOD)/src/pymergetic/util/mtar/__impl__.rs \
		$(WASMMOD)/src/pymergetic/util/version/__impl__.rs \
		$(WASMMOD)/src/pymergetic/wasmmod/registry/__impl__.rs \
		$(WASMMOD)/src/pymergetic/wasmmod/loader/__impl__.rs \
		$(WASMMOD)/src/pymergetic/wasmmod/api/__impl__.rs \
		$(METAL_SRC)/pymergetic/metal/net/http/asgi/__impl__.rs | $(BUILD)
	$(RUSTC) --edition 2024 --crate-type staticlib --crate-name fw_lock \
		--target $(FW_RUSTC_TARGET) -C panic=abort -C opt-level=s \
		$(FW_RUSTC_REDZONE) $(FW_RUSTC_CPU) \
		-o $@ $<

FW_OBJS += $(BUILD)/metal_platform.o $(BUILD)/hello_pack.o
FW_WAMR_LIBS := $(BUILD)/libfw_lock.a $(BUILD)/libwasmmod_wamr_freestanding.a
