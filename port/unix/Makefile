# Linux userspace metal seat (curl-and-run / manylinux).
#
#   make -C extmod/metal/port/unix
#   → ports/unix/build-metal/micropython
#
# i686 (prefer i686-linux-gnu-gcc; else clang -m32):
#   make -C extmod/metal/port/unix \
#     CC='clang -m32' \
#     BUILD=$(CURDIR)/../../../../ports/unix/build-metal-i686 \
#     CFLAGS_EXTRA='-UPM_METAL_CFG_ARCH_X86_64 -DPM_METAL_CFG_ARCH_X86=1 -m32'

# Absolute paths so BUILD/<src>.o cannot collide across x86_64 / i686 builds
# (relative ../../extmod/... collapses to the same ports/extmod/ tree).
UNIX_PORT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../ports/unix)
METAL_ABS := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
VARIANT_DIR := $(METAL_ABS)/port/unix/variant
BUILD ?= $(UNIX_PORT)/build-metal
PROG ?= micropython

.PHONY: all clean
.DEFAULT_GOAL := all

all:
	$(MAKE) -C $(UNIX_PORT) \
		VARIANT_DIR=$(VARIANT_DIR) \
		BUILD=$(BUILD) \
		PROG=$(PROG) \
		MICROPY_MANIFEST_METAL=$(METAL_ABS) \
		$(if $(CC),CC="$(CC)") \
		$(if $(CROSS_COMPILE),CROSS_COMPILE=$(CROSS_COMPILE)) \
		$(if $(CFLAGS_EXTRA),CFLAGS_EXTRA="$(CFLAGS_EXTRA)") \
		$(if $(LDFLAGS_EXTRA),LDFLAGS_EXTRA="$(LDFLAGS_EXTRA)") \
		-j$(shell nproc 2>/dev/null || echo 4)

clean:
	$(MAKE) -C $(UNIX_PORT) VARIANT_DIR=$(VARIANT_DIR) BUILD=$(BUILD) clean
