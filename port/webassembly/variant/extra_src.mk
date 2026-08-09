# Linked after ports/webassembly sets OBJ — not part of SRC_QSTR.
ifeq ($(strip $(METAL_BOOT_SRCS)),)
$(error METAL_BOOT_SRCS empty — mpconfigvariant.mk must define it)
endif

METAL_BOOT_OBJ := $(patsubst $(METAL)/%.c,$(BUILD)/metal/%.o,$(METAL_BOOT_SRCS))
OBJ += $(METAL_BOOT_OBJ)

# Browser RS: fs/util muscle; C owns rt ABI (rt_block.c).
# Keep .a out of OBJ (mkrules -include *.d would parse the archive as a makefile).
RUST_TARGET := wasm32-unknown-unknown
override CARGO_TARGET_DIR := $(abspath $(BUILD)/cargo)
export CARGO_TARGET_DIR
CARGO ?= cargo
METAL_BROWSER_RS_LIB := $(CARGO_TARGET_DIR)/$(RUST_TARGET)/release/libpymergetic_metal_browser_rs.a
LDFLAGS += $(METAL_BROWSER_RS_LIB)

.PHONY: metal-browser-rs
metal-browser-rs: $(METAL_BROWSER_RS_LIB)

$(METAL_BROWSER_RS_LIB): $(METAL)/Cargo.toml $(METAL)/crates/pymergetic_metal_browser_rs/Cargo.toml $(METAL)/crates/pymergetic_metal_browser_rs/src/lib.rs
	$(ECHO) "CARGO pymergetic_metal_browser_rs ($(RUST_TARGET))"
	$(Q)$(CARGO) build --release --target $(RUST_TARGET) \
		--manifest-path $(METAL)/Cargo.toml -p pymergetic_metal_browser_rs

$(BUILD)/metal/%.o: $(METAL)/%.c
	$(ECHO) "CC $<"
	$(Q)$(MKDIR) -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/micropython.mjs: $(METAL_BROWSER_RS_LIB)
