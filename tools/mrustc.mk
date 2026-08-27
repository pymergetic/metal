# Vendored mrustc (externals/mrustc) — the build-time Rust→C front end.
#
# mrustc is a build-time tool, not a runtime card (see the "one-defining-lang"
# and "source-in-its-lang" rules): for every `impl = rs` module, the build runs
# mrustc once to emit `__rust2c__.c` alongside `__impl__.rs`; that `.c` rides
# in the pack's source chromosome. At runtime only TCC exists and compiles the
# pre-converted C. The `.rs` stays the canonical source of truth.
#
#   make -f tools/mrustc.mk rust2c RS_SRC=<path/to/lib.rs> OUT=<path/to/out.c>
#
# Master is validated against rustc 1.90 (libcore/liballoc/libstd/libtest all
# bootstrap with `make -f minicargo.mk RUSTC_VERSION=1.90.0 LIBS`). The default
# 1.29 target-ver in the tree is bit-rotted (fails on libcore/sync/atomic.rs
# asm!), so every invocation below pins MRUSTC_TARGET_VER=1.90.

ifndef PM_METAL_MRUSTC_MK
PM_METAL_MRUSTC_MK := 1

MRUSTC_DIR ?= $(CURDIR)/externals/mrustc
MRUSTC_BIN ?= $(MRUSTC_DIR)/bin/mrustc
# Output of `make -f minicargo.mk RUSTC_VERSION=1.90.0 LIBS` — the rlib search
# path mrustc needs to resolve `core`/`std`/`alloc` extern crates.
MRUSTC_OUT ?= $(MRUSTC_DIR)/output-1.90.0
MRUSTC_TARGET_VER := 1.90

# rust2c: Rust -> generated C. mrustc emits `<out>.c` as its codegen
# intermediate (trans/codegen_c.cpp CodeGenerator_C) and, for rlib output,
# stops before the final gcc compile/link, leaving the `.c` on disk.
# There is no `--output-c` flag.
rust2c: $(MRUSTC_BIN)
	MRUSTC_TARGET_VER=$(MRUSTC_TARGET_VER) $(MRUSTC_BIN) \
		$(RS_SRC) -o $(basename $(OUT)) --crate-type rlib -L $(MRUSTC_OUT)
	mv $(basename $(OUT)).c $(OUT)

$(MRUSTC_BIN):
	$(MAKE) -C $(MRUSTC_DIR) -j

endif
