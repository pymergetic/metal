# Freestanding Rust product crates → one staticlib linked into BIOS/UEFI.
# Not forge. Not .pm. Workspace: $(METAL)/Cargo.toml

CARGO ?= cargo
RUST_TARGET ?= x86_64-unknown-none
# Force workspace-local target dir (ignore ambient CARGO_TARGET_DIR from IDE/sandbox).
override CARGO_TARGET_DIR := $(abspath $(BUILD)/cargo)
export CARGO_TARGET_DIR

METAL_RS_LIB := $(CARGO_TARGET_DIR)/$(RUST_TARGET)/release/libpymergetic_metal_rs.a
# Back-compat name used by board makefiles.
METAL_RT_LIB := $(METAL_RS_LIB)

METAL_RS_SRCS := \
	$(METAL)/Cargo.toml \
	$(METAL)/.cargo/config.toml \
	$(METAL)/crates/pymergetic_metal_rs/Cargo.toml \
	$(METAL)/crates/pymergetic_metal_rs/src/lib.rs \
	$(METAL)/crates/pymergetic_metal_rt/Cargo.toml \
	$(METAL)/src/pymergetic/metal/rt/__init__.rs \
	$(METAL)/src/pymergetic/metal/rt/ffi.rs \
	$(METAL)/crates/pymergetic_metal_async/Cargo.toml \
	$(METAL)/src/pymergetic/metal/async/__init__.rs \
	$(METAL)/crates/pymergetic_metal_fs_vfs/Cargo.toml \
	$(METAL)/src/pymergetic/metal/fs/vfs/__init__.rs \
	$(METAL)/crates/pymergetic_metal_fs/Cargo.toml \
	$(METAL)/src/pymergetic/metal/fs/__init__.rs \
	$(METAL)/src/pymergetic/metal/fs/ops.rs \
	$(METAL)/crates/pymergetic_metal_fs_tmpfs/Cargo.toml \
	$(METAL)/src/pymergetic/metal/fs/tmpfs/__init__.rs \
	$(METAL)/crates/pymergetic_metal_fs_wasmmod/Cargo.toml \
	$(METAL)/src/pymergetic/metal/fs/wasmmod/__init__.rs \
	$(METAL)/crates/pymergetic_metal_util_lz4/Cargo.toml \
	$(METAL)/src/pymergetic/metal/util/lz4/__init__.rs \
	$(METAL)/crates/pymergetic_metal_util_tar/Cargo.toml \
	$(METAL)/src/pymergetic/metal/util/tar/__init__.rs \
	$(METAL)/crates/pymergetic_metal_util_size/Cargo.toml \
	$(METAL)/src/pymergetic/metal/util/size/__init__.rs \
	$(METAL)/crates/pymergetic_metal_fs_mtar/Cargo.toml \
	$(METAL)/src/pymergetic/metal/fs/mtar/__init__.rs

.PHONY: metal-rt metal-rs
metal-rt metal-rs: $(METAL_RS_LIB)

$(METAL_RS_LIB): $(METAL_RS_SRCS)
	$(ECHO) "CARGO pymergetic_metal_rs ($(RUST_TARGET))"
	$(Q)$(CARGO) build --release --target $(RUST_TARGET) \
		--manifest-path $(METAL)/Cargo.toml -p pymergetic_metal_rs
