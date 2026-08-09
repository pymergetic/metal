# metal CDN `mp`: wasmmod browser flags + live C boot (same as box seats).
MICROPY_PY_WASM = 1
WASMMOD_EMSCRIPTEN = 1

JSFLAGS += -s ASYNCIFY
JSFLAGS += -s ASYNCIFY_STACK_SIZE=65536
# CDN heap selector (up to 128 MiB GC) needs growable linear memory.
JSFLAGS += -s ALLOW_MEMORY_GROWTH

CFLAGS += -DMICROPY_WASM_HTTP_NATIVE=0

MICROPY_PY_SSL = 1
MICROPY_SSL_MBEDTLS = 1
MICROPY_WASM_VERIFY ?= 1
MICROPY_WASM_TRUST_CA ?= $(TOP)/extmod/wasmmod/examples/.keys/trust/root.crt.der

METAL := $(TOP)/extmod/metal
MICROPY_MANIFEST_METAL := $(METAL)
FROZEN_MANIFEST := $(METAL)/port/manifest_wasm.py

# Nested pymergetic.metal.* (wasmmod owns root; metal child nested in wasmmod.c).
CFLAGS += -DMICROPY_MODULE_BUILTIN_SUBPACKAGES=1

# Do not -I port/upy (ships mpconfigport.h and would shadow webassembly).
METAL_CDN_URL ?= https://cdn.pymergetic.com/cdn
METAL_CDN_EXTRA_URLS ?=

CFLAGS += -I$(METAL)/include -I$(METAL)/port/hal \
	-I$(METAL)/port/boot -I$(METAL)/glue \
	-I$(METAL)/src/pymergetic/metal/util \
	-I$(METAL)/third_party/tlsf \
	-I$(METAL)/third_party/monocypher \
	-I$(METAL)/third_party/sha256 \
	-DPM_METAL_CFG_ARCH_WASM=1 -DPM_METAL_CFG_FW_BROWSER=1 \
	-DMETAL_CDN_URL=\"$(METAL_CDN_URL)\" \
	-DMETAL_CDN_EXTRA_URLS=\"$(METAL_CDN_EXTRA_URLS)\"

# Live boot + CORE faces via variant/extra_src.mk.
# Absolute paths for METAL_BOOT_SRCS; SRC_QSTR for nest qstrs.
METAL_BOOT_SRCS := \
	$(METAL)/port/boot/boot.c \
	$(METAL)/port/boot/autoexec.c \
	$(METAL)/port/boot/cdn_cfg.c \
	$(METAL)/glue/pymergetic/metal/__init__.c \
	$(METAL)/glue/pymergetic/metal/externals.c \
	$(METAL)/glue/pymergetic/metal/auth.c \
	$(METAL)/glue/pymergetic/metal/trust.c \
	$(METAL)/glue/pymergetic/metal/util/__init__.c \
	$(METAL)/glue/pymergetic/metal/util/lz4.c \
	$(METAL)/glue/pymergetic/metal/util/size.c \
	$(METAL)/glue/pymergetic/metal/util/endian.c \
	$(METAL)/glue/pymergetic/metal/util/fourcc.c \
	$(METAL)/glue/pymergetic/metal/util/eightcc.c \
	$(METAL)/glue/pymergetic/metal/util/ascii.c \
	$(METAL)/port/hal/wasm/console.c \
	$(METAL)/port/hal/wasm/mem.c \
	$(METAL)/src/pymergetic/metal/boot/tree.c \
	$(METAL)/src/pymergetic/metal/boot/externals.c \
	$(METAL)/src/pymergetic/metal/boot/externals_rows.c \
	$(METAL)/src/pymergetic/metal/arch/arch.c \
	$(METAL)/src/pymergetic/metal/arch/py_call.c \
	$(METAL)/src/pymergetic/metal/arch/wasm/bridge.c \
	$(METAL)/src/pymergetic/metal/arch/x86/bridge.c \
	$(METAL)/src/pymergetic/metal/arch/x86_64/bridge.c \
	$(METAL)/src/pymergetic/metal/net/microdot/bridge.c \
	$(METAL)/src/pymergetic/metal/auth/__init__.c \
	$(METAL)/src/pymergetic/metal/trust/__init__.c \
	$(METAL)/src/pymergetic/metal/util/ascii.c \
	$(METAL)/src/pymergetic/metal/util/lz4/lz4_block.c \
	$(METAL)/src/pymergetic/metal/util/size/size_format.c \
	$(METAL)/src/pymergetic/metal/util/endian/__init__.c \
	$(METAL)/src/pymergetic/metal/util/fourcc/__init__.c \
	$(METAL)/src/pymergetic/metal/util/eightcc/__init__.c \
	$(METAL)/src/pymergetic/metal/mem/port/mem.c \
	$(METAL)/third_party/tlsf/tlsf.c \
	$(METAL)/third_party/monocypher/monocypher.c \
	$(METAL)/third_party/monocypher/monocypher-ed25519.c \
	$(METAL)/third_party/sha256/sha256.c

# Attribute qstrs from metal glue faces (dotted module names come from these TUs).
SRC_QSTR += \
	$(METAL)/glue/pymergetic/metal/__init__.c \
	$(METAL)/glue/pymergetic/metal/externals.c \
	$(METAL)/glue/pymergetic/metal/auth.c \
	$(METAL)/glue/pymergetic/metal/trust.c \
	$(METAL)/glue/pymergetic/metal/util/__init__.c \
	$(METAL)/glue/pymergetic/metal/util/lz4.c \
	$(METAL)/glue/pymergetic/metal/util/size.c \
	$(METAL)/glue/pymergetic/metal/util/endian.c \
	$(METAL)/glue/pymergetic/metal/util/fourcc.c \
	$(METAL)/glue/pymergetic/metal/util/eightcc.c \
	$(METAL)/glue/pymergetic/metal/util/ascii.c
