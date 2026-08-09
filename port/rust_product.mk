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
	$(METAL)/src/pymergetic/metal/async/quiesce.rs \
	$(METAL)/crates/pymergetic_metal_dt/Cargo.toml \
	$(METAL)/src/pymergetic/metal/dt/__init__.rs \
	$(METAL)/crates/pymergetic_metal_log/Cargo.toml \
	$(METAL)/src/pymergetic/metal/log/__init__.rs \
	$(METAL)/crates/pymergetic_metal_mem_lock/Cargo.toml \
	$(METAL)/src/pymergetic/metal/mem/lock/__init__.rs \
	$(METAL)/src/pymergetic/metal/mem/lock/mutex.rs \
	$(METAL)/src/pymergetic/metal/mem/lock/spin.rs \
	$(METAL)/crates/pymergetic_metal_mem_arena/Cargo.toml \
	$(METAL)/src/pymergetic/metal/mem/arena/__init__.rs \
	$(METAL)/crates/pymergetic_metal_mem_tlsf/Cargo.toml \
	$(METAL)/src/pymergetic/metal/mem/tlsf/__init__.rs \
	$(METAL)/crates/pymergetic_metal_mem/Cargo.toml \
	$(METAL)/src/pymergetic/metal/mem/__init__.rs \
	$(METAL)/crates/pymergetic_metal_reg/Cargo.toml \
	$(METAL)/src/pymergetic/metal/reg/__init__.rs \
	$(METAL)/src/pymergetic/metal/reg/_ledger.rs \
	$(METAL)/src/pymergetic/metal/reg/_floor.rs \
	$(METAL)/src/pymergetic/metal/reg/_kernel.rs \
	$(METAL)/src/pymergetic/metal/reg/_entry.rs \
	$(METAL)/src/pymergetic/metal/reg/_table.rs \
	$(METAL)/crates/pymergetic_metal_fs_vfs/Cargo.toml \
	$(METAL)/src/pymergetic/metal/fs/vfs/__init__.rs \
	$(METAL)/crates/pymergetic_metal_fs/Cargo.toml \
	$(METAL)/src/pymergetic/metal/fs/__init__.rs \
	$(METAL)/src/pymergetic/metal/fs/ops.rs \
	$(METAL)/crates/pymergetic_metal_fs_tmpfs/Cargo.toml \
	$(METAL)/src/pymergetic/metal/fs/tmpfs/__init__.rs \
	$(METAL)/crates/pymergetic_metal_fs_wasmmod/Cargo.toml \
	$(METAL)/src/pymergetic/metal/fs/wasmmod/__init__.rs \
	$(METAL)/crates/pymergetic_metal_fs_embed/Cargo.toml \
	$(METAL)/src/pymergetic/metal/fs/embed/__init__.rs \
	$(METAL)/crates/pymergetic_metal_fs_zip/Cargo.toml \
	$(METAL)/src/pymergetic/metal/fs/zip/__init__.rs \
	$(METAL)/crates/pymergetic_metal_dev_blk_ram/Cargo.toml \
	$(METAL)/src/pymergetic/metal/dev/blk/ram/__init__.rs \
	$(METAL)/crates/pymergetic_metal_fs_fat/Cargo.toml \
	$(METAL)/src/pymergetic/metal/fs/fat/__init__.rs \
	$(METAL)/crates/pymergetic_metal_fs_overlay/Cargo.toml \
	$(METAL)/src/pymergetic/metal/fs/overlay/__init__.rs \
	$(METAL)/crates/pymergetic_metal_fs_littlefs/Cargo.toml \
	$(METAL)/src/pymergetic/metal/fs/littlefs/__init__.rs \
	$(METAL)/crates/pymergetic_metal_util_lz4/Cargo.toml \
	$(METAL)/src/pymergetic/metal/util/lz4/__init__.rs \
	$(METAL)/crates/pymergetic_metal_util_tar/Cargo.toml \
	$(METAL)/src/pymergetic/metal/util/tar/__init__.rs \
	$(METAL)/crates/pymergetic_metal_util_size/Cargo.toml \
	$(METAL)/src/pymergetic/metal/util/size/__init__.rs \
	$(METAL)/crates/pymergetic_metal_fs_mtar/Cargo.toml \
	$(METAL)/src/pymergetic/metal/fs/mtar/__init__.rs \
	$(METAL)/crates/pymergetic_metal_hwtree/Cargo.toml \
	$(METAL)/src/pymergetic/metal/hwtree/__init__.rs \
	$(METAL)/crates/pymergetic_metal_wamr_host/Cargo.toml \
	$(METAL)/src/pymergetic/metal/wamr_host/__init__.rs

.PHONY: metal-rt metal-rs
metal-rt metal-rs: $(METAL_RS_LIB)

$(METAL_RS_LIB): $(METAL_RS_SRCS)
	$(ECHO) "CARGO pymergetic_metal_rs ($(RUST_TARGET))"
	$(Q)$(CARGO) build --release --target $(RUST_TARGET) \
		--manifest-path $(METAL)/Cargo.toml -p pymergetic_metal_rs
