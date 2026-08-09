# metal unix host seat (curl-and-run Linux µPy).
# abspath: relative TOP/../../extmod would collapse BUILD/<rel> objs across arches.
METAL := $(abspath $(TOP)/extmod/metal)
MICROPY_MANIFEST_METAL := $(METAL)
FROZEN_MANIFEST := $(METAL)/port/manifest_unix.py
METAL_CDN_URL ?= https://cdn.pymergetic.com/cdn
# Compile-time GC heap (bytes). Runtime: METAL_HEAPSIZE / MICROPY_HEAPSIZE / -X heapsize=
MICROPY_HEAP_SIZE ?= 4194304

CFLAGS += -I$(METAL)/include -I$(METAL)/src -I$(METAL)/third_party/tlsf
CFLAGS += -DPM_METAL_CFG_FW_UNIX=1
# default x86_64; override with CFLAGS_EXTRA=-DPM_METAL_CFG_ARCH_X86=1 for i686
CFLAGS += -DPM_METAL_CFG_ARCH_X86_64=1
CFLAGS += -DMETAL_CDN_URL=\"$(METAL_CDN_URL)\"
CFLAGS += -DMICROPY_HEAP_SIZE=$(MICROPY_HEAP_SIZE)

# Prefer portable dynamic link for v1 (static often fails without musl).
MICROPY_PY_FFI = 0

# Bare argv → boot.tree + -i -m pymergetic.metal.unix (wrap via VARIANT_DIR *.c).
LDFLAGS_EXTRA += -Wl,--wrap=main

# arch face for pm_metal_arch_firmware() / CFG (VARIANT_DIR *.c is already picked up).
WASMMOD := $(abspath $(TOP)/extmod/wasmmod)
CFLAGS += -I$(WASMMOD)/include -I$(WASMMOD)
SRC_C += $(METAL)/src/pymergetic/metal/arch/arch.c
# Live boot.tree + FIGlet (same muscle as FW/browser).
# unix_boot.c is VARIANT_DIR/*.c (auto); tree/ascii are not.
SRC_C += \
	$(METAL)/src/pymergetic/metal/boot/tree.c \
	$(METAL)/src/pymergetic/metal/util/ascii.c
# Into-Py unix seat bridges + wasmmod pm_upy_* bus (MICROPY_PY_WASM=0 → handle stub).
SRC_C += \
	$(METAL)/src/pymergetic/metal/unix/x86/bridge.c \
	$(METAL)/src/pymergetic/metal/unix/x86_64/bridge.c \
	$(WASMMOD)/glue/pm_upy/obj/core.c \
	$(WASMMOD)/glue/pm_upy/obj/call.c \
	$(WASMMOD)/glue/pm_upy/obj/module.c \
	$(WASMMOD)/glue/pm_upy/obj/ops.c \
	$(METAL)/port/upy/wasm_handles_stub.c \
	$(METAL)/src/pymergetic/metal/process/__init__.c \
	$(METAL)/src/pymergetic/metal/boot/unboot.c \
	$(METAL)/src/pymergetic/metal/async/__init__.c \
	$(METAL)/src/pymergetic/metal/async/meter.c \
	$(METAL)/src/pymergetic/metal/mem/port/mem.c \
	$(METAL)/third_party/tlsf/tlsf.c \
	$(METAL)/glue/pymergetic/metal/process.c
# VARIANT_DIR/*.c: orch bind, board_time, smp_stub, quiesce (C).
SRC_QSTR += \
	$(METAL)/port/upy/wasm_handles_stub.c \
	$(METAL)/glue/pymergetic/metal/process.c \
	$(METAL)/port/unix/variant/metal_unix_orch.c

PROG ?= micropython

# Dotted builtin module names (same pool as FW qstrdefsport.h seeds).
QSTR_DEFS += $(METAL)/port/unix/variant/qstrdefsport.h

# Upstream µPy only (metal sources stay strict).
$(BUILD)/py/parse.o: CFLAGS += -Wno-sign-compare
