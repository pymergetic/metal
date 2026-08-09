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
SRC_C += $(METAL)/src/pymergetic/metal/arch/arch.c

PROG ?= micropython
