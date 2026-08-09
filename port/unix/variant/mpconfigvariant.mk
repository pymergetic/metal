# metal unix host seat (curl-and-run Linux µPy).
# abspath: relative TOP/../../extmod would collapse BUILD/<rel> objs across arches.
METAL := $(abspath $(TOP)/extmod/metal)
MICROPY_MANIFEST_METAL := $(METAL)
FROZEN_MANIFEST := $(METAL)/port/manifest_unix.py
METAL_CDN_URL ?= https://cdn.pymergetic.com/cdn
# Compile-time GC heap (bytes). Runtime: METAL_HEAPSIZE / MICROPY_HEAPSIZE / -X heapsize=
MICROPY_HEAP_SIZE ?= 4194304

CFLAGS += -I$(METAL)/include -DPM_METAL_CFG_FW_UNIX=1
# default x86_64; override with CFLAGS_EXTRA=-DPM_METAL_CFG_ARCH_X86=1 for i686
CFLAGS += -DPM_METAL_CFG_ARCH_X86_64=1
CFLAGS += -DMETAL_CDN_URL=\"$(METAL_CDN_URL)\"
CFLAGS += -DMICROPY_HEAP_SIZE=$(MICROPY_HEAP_SIZE)

# Prefer portable dynamic link for v1 (static often fails without musl).
MICROPY_PY_FFI = 0

# Bare argv → -m pymergetic.metal.unix (metal_unix_wrap.c via VARIANT_DIR *.c).
LDFLAGS_EXTRA += -Wl,--wrap=main

# arch face for pm_metal_arch_firmware() / CFG (VARIANT_DIR *.c is already picked up).
WASMMOD := $(abspath $(TOP)/extmod/wasmmod)
CFLAGS += -I$(WASMMOD)/include -I$(WASMMOD)
SRC_C += $(METAL)/src/pymergetic/metal/arch/arch.c
# Into-Py unix seat bridges + wasmmod pm_upy_* bus (MICROPY_PY_WASM=0 → handle stub).
SRC_C += \
	$(METAL)/src/pymergetic/metal/unix/x86/bridge.c \
	$(METAL)/src/pymergetic/metal/unix/x86_64/bridge.c \
	$(WASMMOD)/glue/pm_upy/obj/core.c \
	$(WASMMOD)/glue/pm_upy/obj/call.c \
	$(WASMMOD)/glue/pm_upy/obj/module.c \
	$(WASMMOD)/glue/pm_upy/obj/ops.c \
	$(METAL)/port/upy/wasm_handles_stub.c
SRC_QSTR += $(METAL)/port/upy/wasm_handles_stub.c

PROG ?= micropython

# Upstream µPy only (metal sources stay strict).
$(BUILD)/py/parse.o: CFLAGS += -Wno-sign-compare
