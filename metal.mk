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

CFLAGS_EXTMOD += -DMICROPY_PY_METAL=1
CFLAGS_EXTMOD += -include $(TOP)/extmod/metal/mpconfig_unix.h
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

# io_ops.c is wasmmod-owned; Metal compiles it (not SRC_WASMMOD / cargo / CPython).
SRC_METAL_CORE = \
	extmod/metal/modmetal.c \
	extmod/metal/boot.c \
	extmod/wasmmod/ports/metal/io_ops.c \
	extmod/metal/src/pymergetic/metal/dt/__impl__.c \
	extmod/metal/src/pymergetic/metal/bus/pci/__impl__.c \
	extmod/metal/src/pymergetic/metal/bus/virtio/__impl__.c \
	extmod/metal/src/pymergetic/metal/drivers/__impl__.c \
	extmod/metal/src/pymergetic/metal/drivers/net/__impl__.c \
	extmod/metal/src/pymergetic/metal/drivers/blk/__impl__.c \
	extmod/metal/src/pymergetic/metal/drivers/blk/virtio/__impl__.c \
	extmod/metal/src/pymergetic/metal/drivers/rtc/__impl__.c \
	extmod/metal/src/pymergetic/metal/drivers/rtc/sim/__impl__.c \
	extmod/metal/src/pymergetic/metal/fw/memmap/__impl__.c \
	extmod/metal/src/pymergetic/metal/async/__impl__.c \
	extmod/metal/src/pymergetic/metal/net/ip/__impl__.c \
	extmod/metal/src/pymergetic/metal/net/tls/__impl__.c \
	extmod/metal/src/pymergetic/metal/net/http/__impl__.c \
	extmod/metal/src/pymergetic/metal/net/dns/__impl__.c \
	extmod/metal/src/pymergetic/metal/drivers/net/sim/__impl__.c \
	extmod/metal/src/pymergetic/metal/net/wg/__crypto__.c \
	extmod/metal/src/pymergetic/metal/net/wg/__impl__.c \
	extmod/metal/src/pymergetic/metal/net/ntp/__impl__.c \
	extmod/metal/src/pymergetic/metal/net/dhcp/__impl__.c \
	extmod/metal/src/pymergetic/metal/net/tftp/__impl__.c \
	extmod/metal/src/pymergetic/metal/net/ssh/__impl__.c

# unix / firmware NICs and ISA leaves — not in the browser cell (emcc).
SRC_METAL_HOST = \
	extmod/metal/src/pymergetic/metal/drivers/net/tap/__impl__.c \
	extmod/metal/src/pymergetic/metal/drivers/net/virtio/__impl__.c \
	extmod/metal/src/pymergetic/metal/drivers/net/bge/__impl__.c \
	extmod/metal/src/pymergetic/metal/drivers/blk/ide/__impl__.c \
	extmod/metal/src/pymergetic/metal/drivers/rtc/cmos/__impl__.c

ifdef PM_METAL_BROWSER
SRC_METAL_C = $(SRC_METAL_CORE) extmod/metal/modpymergetic.c
# Extra --js-library; leave ports/webassembly/library.js vanilla.
JSFLAGS += --js-library $(TOP)/extmod/metal/src/pymergetic/metal/drivers/net/sim/library.js
JSFLAGS += -s ALLOW_MEMORY_GROWTH=1
JSFLAGS += -s INITIAL_MEMORY=33554432
$(BUILD)/micropython.mjs: $(TOP)/extmod/metal/src/pymergetic/metal/drivers/net/sim/library.js
$(addprefix $(BUILD)/, $(SRC_METAL_C:.c=.o)): CFLAGS += -std=gnu99
else
SRC_METAL_C = $(SRC_METAL_CORE) $(SRC_METAL_HOST)
endif

PY_O += $(addprefix $(BUILD)/, $(SRC_METAL_C:.c=.o))
SRC_QSTR += $(SRC_METAL_C)
QSTR_DEFS += $(TOP)/extmod/metal/qstrdefs.metal
