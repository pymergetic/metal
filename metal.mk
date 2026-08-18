# Metal as a µPy extmod. Heap is pymergetic.util.mem — do not compile tlsf here.
#
#   make -C ports/unix MICROPY_PY_WASM=1 MICROPY_PY_METAL=1 BUILD=build-metal
#   make -C extmod/metal test
#
# Requires wasmmod (pm_util_mem_* in libpymergetic_wasmmod.a).

ifeq ($(MICROPY_PY_WASM),)
$(error MICROPY_PY_METAL=1 needs MICROPY_PY_WASM=1 (util.mem lives in wasmmod))
endif

# ports/webassembly sets CC=emcc after including extmod.mk, so do not key off $(CC).
ifeq ($(notdir $(CURDIR)),webassembly)
PM_METAL_BROWSER := 1
endif

WASMMOD_GEN_ROOTS := $(TOP)/extmod/metal/src
include $(TOP)/extmod/wasmmod/gen.mk
include $(TOP)/extmod/metal/tools/www.mk

# Metal's crate depends on wasmmod, so libpymergetic_metal.a already contains
# it. Claim the seat's cargo build here and wasmmod's fragment links ours
# instead of a second archive with the same wasmmod objects in it.
WASMMOD_CARGO_DIR := $(TOP)/extmod/metal
WASMMOD_CARGO_LIB := pymergetic_metal

CFLAGS_EXTMOD += -DMICROPY_PY_METAL=1
CFLAGS_EXTMOD += -include $(TOP)/extmod/metal/mpconfig_unix.h
# Blown-in `__bench__.c` cards self-register benches into the registry behind
# the same `-DPM_MOD_BENCHES=1` the host bench binary uses. Benches never gate;
# the seat's bench clock (metal mono_us, installed at boot) decides whether a
# REPL call to wm.bench_all() reports ns/op or an honest "no clock".
CFLAGS_EXTMOD += -DPM_MOD_BENCHES=1
INC += -I$(TOP)/extmod/metal/src

# wg X25519 + net.tls use vendored mbedtls. Unix SSL already compiles it;
# the browser seat does not unless we add the library here.
MBEDTLS_DIR ?= lib/mbedtls
INC += -I$(TOP)/$(MBEDTLS_DIR)/include
CFLAGS_EXTMOD += -I$(TOP)/$(MBEDTLS_DIR)/include
CFLAGS_EXTMOD += -DMBEDTLS_CONFIG_FILE='"mbedtls/mbedtls_config_port.h"'
CFLAGS_EXTMOD += -DMICROPY_SSL_MBEDTLS=1
ifdef PM_METAL_BROWSER
# mbedtls_config_port.h lives under wasmmod/ports/common/mbedtls/.
# Do not -I ports/unix: that shadows the webassembly mpconfigport.h (PATH_MAX).
INC += -I$(TOP)/extmod/wasmmod/ports/common
CFLAGS_EXTMOD += -I$(TOP)/extmod/wasmmod/ports/common
endif
ifneq ($(MICROPY_PY_SSL),1)
SRC_METAL_MBEDTLS = $(addprefix $(MBEDTLS_DIR)/library/,\
	aes.c aesni.c asn1parse.c asn1write.c base64.c bignum_core.c bignum_mod.c \
	bignum_mod_raw.c bignum.c camellia.c ccm.c chacha20.c chachapoly.c cipher.c \
	cipher_wrap.c nist_kw.c aria.c cmac.c constant_time.c mps_reader.c mps_trace.c \
	ctr_drbg.c debug.c des.c dhm.c ecdh.c ecdsa.c ecjpake.c ecp.c ecp_curves.c \
	entropy.c entropy_poll.c gcm.c hmac_drbg.c md5.c md.c oid.c padlock.c pem.c \
	pk.c pkcs12.c pkcs5.c pkparse.c pk_ecc.c pk_wrap.c pkwrite.c platform.c \
	platform_util.c poly1305.c ripemd160.c rsa.c rsa_alt_helpers.c sha1.c sha256.c \
	sha512.c ssl_cache.c ssl_ciphersuites.c ssl_client.c ssl_cookie.c \
	ssl_debug_helpers_generated.c ssl_msg.c ssl_ticket.c ssl_tls.c ssl_tls12_client.c \
	ssl_tls12_server.c timing.c x509.c x509_create.c x509_crl.c x509_crt.c x509_csr.c \
	x509write_crt.c x509write_csr.c)
PY_O += $(addprefix $(BUILD)/, $(SRC_METAL_MBEDTLS:.c=.o))
endif

# Cards come from the tree, not from a list here — see tools/cards.sh. The same
# TUs land on unix, firmware, and emcc; fills already #ifdef (tap is linux,
# virtio MMIO is firmware, cmos ports are firmware, the rest is plain C).
METAL_CARD_REL := $(shell $(TOP)/extmod/metal/tools/cards.sh impl \
	$(TOP)/extmod/metal/src/pymergetic/metal)
ifeq ($(METAL_CARD_REL),)
$(error metal card discovery failed — see tools/cards.sh output above)
endif

# io_ops.c is wasmmod-owned; Metal compiles it (not SRC_WASMMOD / cargo / CPython).
SRC_METAL_CORE = \
	extmod/metal/modmetal.c \
	extmod/metal/boot.c \
	extmod/wasmmod/ports/freestanding/io_ops.c

SRC_METAL_C = $(SRC_METAL_CORE) \
	$(addprefix extmod/metal/src/pymergetic/metal/,$(METAL_CARD_REL))
ifdef PM_METAL_BROWSER
# The emcc cell needs no platform from us: wasmmod owns WAMR + interp;
# metal's fw_lock (ASGI + lock/registry) is selected in micropython.mk
# when MICROPY_PY_METAL=1. Extra --js-library; leave library.js vanilla.
JSFLAGS += --js-library $(TOP)/extmod/metal/src/pymergetic/metal/drivers/net/sim/library.js
JSFLAGS += -s ALLOW_MEMORY_GROWTH=1
JSFLAGS += -s INITIAL_MEMORY=33554432
$(BUILD)/micropython.mjs: $(TOP)/extmod/metal/src/pymergetic/metal/drivers/net/sim/library.js
$(addprefix $(BUILD)/, $(SRC_METAL_C:.c=.o)): CFLAGS += -std=gnu99
endif

PY_O += $(addprefix $(BUILD)/, $(SRC_METAL_C:.c=.o))
SRC_QSTR += $(SRC_METAL_C)
QSTR_DEFS += $(TOP)/extmod/metal/qstrdefs.metal
