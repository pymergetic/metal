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
include $(TOP)/extmod/metal/tools/src.mk

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

# net.zenoh: vendored zenoh-pico core, same GENERIC config/platform headers the
# host (metal.mk) and firmware (fw_zenoh.mk) seats use. The card border + platform
# shim come from the card tree (METAL_CARD_REL); only the vendored core is listed
# here, mbedtls-style, from the shared source glob.
ZENOH_PICO_DIR ?= $(TOP)/extmod/metal/externals/zenoh-pico
ZENOH_CARD_DIR ?= $(TOP)/extmod/metal/src/pymergetic/metal/net/zenoh
include $(TOP)/extmod/metal/tools/zenoh.mk
SRC_METAL_ZENOH = $(addprefix extmod/metal/externals/zenoh-pico/,$(ZP_REL))
PY_O += $(addprefix $(BUILD)/, $(SRC_METAL_ZENOH:.c=.o))
INC += -I$(ZENOH_PICO_DIR)/include -I$(ZENOH_PICO_DIR)/src -I$(ZENOH_CARD_DIR)
CFLAGS_EXTMOD += -DZENOH_GENERIC

# vendored TCC (externals/tcc) — ONE_SOURCE=1: only libtcc.c compiles all others via #include
# The browser seat runs as wasm32 (emcc), so its embedded TCC targets WASM32:
# C source is JIT'd to WASM and fed to WAMR, never to native x86_64. The unix
# µPy seat is a native x86_64 process, so it keeps the native backend.
TCC_DIR ?= $(TOP)/extmod/metal/externals/tcc
PY_O += $(BUILD)/externals/tcc/libtcc.o
ifdef PM_METAL_BROWSER
CFLAGS_EXTMOD += -DTCC_TARGET_WASM32 -DPM_HAS_TCC=1 -DPM_METAL_TCC_LIB_DIR=\"$(TCC_DIR)\"
else
CFLAGS_EXTMOD += -DTCC_TARGET_X86_64 -DPM_HAS_TCC=1 -DPM_METAL_TCC_LIB_DIR=\"$(TCC_DIR)\"
endif
INC += -I$(TCC_DIR)

$(BUILD)/externals/tcc/libtcc.o: $(TCC_DIR)/libtcc.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS_EXTMOD) $(INC) -std=gnu11 -Wno-unused-parameter -Wno-sign-compare -Wno-error -c -o $@ $<

# zenoh-pico's api/macros.h is C11 _Generic; unix µPy defaults to gnu99 and the
# browser seat forces gnu99 on all card objects, so the zenoh card + core objects
# are re-upped to gnu11 (last -std wins). The vendored core's benign warnings must
# also not hard-error under the µPy build's global -Werror (host metal.mk and
# fw_zenoh.mk drop -Werror for the same reason).
# NOTE: the actual CFLAGS install for ZENOH_GN11_OBJS lives AFTER SRC_METAL_C is
# defined below — this is a recursive variable that expands SRC_METAL_C at rule
# install time; installing earlier silently drops the net/zenoh card border
# objects (no gnu11, and c99 rejects zenoh-pico's typedef redefinition).

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

# Zenoh card + core get gnu11 and the core's warnings are suppressed. This must
# live AFTER the per-seat card CFLAGS (the browser block above forces gnu99 on
# every card object): for target-specific CFLAGS the later-defined rule wins for
# a given object, so this gnu11 rule would be silently shadowed by that gnu99 one
# if installed earlier. Not installing it makes c99/gnu99 reject zenoh-pico's
# typedef redefinition (a C11 feature). ZENOH_GN11_OBJS expands SRC_METAL_C here,
# so it does cover net/zenoh/__impl__.o.
# (The border is picked with findstring-in-foreach, NOT $(filter %net/zenoh/%,...):
# make's filter allows only ONE '%' wildcard per pattern, so a two-% pattern like
# %net/zenoh/% silently matches nothing for these card paths.)
ZENOH_CARD_BORDER = $(foreach f,$(SRC_METAL_C),$(if $(findstring /net/zenoh/,$f),$f))
ZENOH_GN11_OBJS = $(addprefix $(BUILD)/, $(ZENOH_CARD_BORDER:.c=.o) $(SRC_METAL_ZENOH:.c=.o))
$(ZENOH_GN11_OBJS): CFLAGS += -std=gnu11
# The vendored core's benign warnings must not hard-error under the µPy build's
# global -Werror (its -Wmaybe-uninitialized etc. are GCC-only selectors that
# clang/emcc rejects outright — so suppress globally, not per-selector, exactly
# like fw_zenoh.mk omits -Werror for the same external tree).
$(addprefix $(BUILD)/, $(SRC_METAL_ZENOH:.c=.o)): CFLAGS += -Wno-error

PY_O += $(addprefix $(BUILD)/, $(SRC_METAL_C:.c=.o))
SRC_QSTR += $(SRC_METAL_C)
QSTR_DEFS += $(TOP)/extmod/metal/qstrdefs.metal
