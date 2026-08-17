# Metal host prove (no µPy). Heap/lock are wasmmod: util.mem (impl=c) and
# util.lock (impl=rs), reached through Metal's own crate — not a C lock twin.
# libpymergetic_metal.a depends on wasmmod and carries it, so this binary links
# one archive; wasmmod itself never mentions Metal.
# Same µPy-vendored mbedtls (lib/mbedtls) unix already compiles.
# host_inc/py/mpconfig.h is only for MBEDTLS_CONFIG_FILE on this binary.
#
#   make -C extmod/metal test
#
WASMMOD ?= $(abspath ../wasmmod)
METAL_SRC ?= $(abspath src)
WASMMOD_SRC ?= $(WASMMOD)/src
TOP ?= $(abspath ../..)
MBEDTLS_DIR ?= $(TOP)/lib/mbedtls

METAL_LIBDIR := $(shell cd $(CURDIR) && cargo metadata --no-deps --format-version 1 2>/dev/null | python3 -c "import sys,json; print(json.load(sys.stdin)['target_directory']+'/release')")
ifeq ($(METAL_LIBDIR),)
METAL_LIBDIR := $(CURDIR)/target/release
endif
METAL_STATICLIB := $(METAL_LIBDIR)/libpymergetic_metal.a
WASMMOD_IWASM_A := $(firstword $(wildcard $(METAL_LIBDIR)/build/*/out/vmlib/build/libiwasm.a))

CC ?= cc
NODE ?= node
CFLAGS ?= -std=gnu11 -Wall -Wextra -Werror -O1 -g -pthread
CPPFLAGS += -I$(CURDIR)/host_inc -I$(METAL_SRC) -I$(WASMMOD_SRC) -I$(WASMMOD) -I$(TOP) \
	-I$(TOP)/ports/unix -I$(MBEDTLS_DIR)/include \
	-D_POSIX_C_SOURCE=200809L -DPM_WASMMOD_GUEST=0 -DPM_MOD_TESTS=1 \
	-DMICROPY_SSL_MBEDTLS=1 \
	-DMBEDTLS_CONFIG_FILE='"mbedtls/mbedtls_config_port.h"'

WASMMOD_GEN_ROOTS := $(CURDIR)/src
include $(WASMMOD)/gen.mk

# Cards and their tests come from the tree (tools/cards.sh), so a new card is
# proven here the moment it has a manifest — nothing to add below.
CARD_REL := $(shell $(CURDIR)/tools/cards.sh impl $(METAL_SRC)/pymergetic/metal) \
	$(shell $(CURDIR)/tools/cards.sh tests $(METAL_SRC)/pymergetic/metal)
ifeq ($(CARD_REL),)
$(error metal card discovery failed — see tools/cards.sh output above)
endif

SRCS := host_test.c \
	$(addprefix $(METAL_SRC)/pymergetic/metal/,$(CARD_REL))

LDFLAGS_WASMMOD := -L$(METAL_LIBDIR) -lpymergetic_metal -lpthread -ldl -lm -lstdc++ -lrt
ifneq ($(WASMMOD_IWASM_A),)
LDFLAGS_WASMMOD += -L$(dir $(WASMMOD_IWASM_A)) -liwasm
endif

MBEDTLS_LIB_SRCS := \
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
	x509write_crt.c x509write_csr.c

MBEDTLS_OBJS := $(addprefix $(CURDIR)/build/mbedtls/, $(MBEDTLS_LIB_SRCS:.c=.o))
MBEDTLS_CPPFLAGS := -I$(CURDIR)/host_inc -I$(TOP) -I$(TOP)/ports/unix \
	-I$(MBEDTLS_DIR)/include -I$(MBEDTLS_DIR)/library \
	-DMBEDTLS_CONFIG_FILE='"mbedtls/mbedtls_config_port.h"' -D_POSIX_C_SOURCE=200809L

OUT := $(CURDIR)/build/metal-async-test
# emcc from PATH, or $EMSDK/upstream/emscripten — do not bake a home directory.
ifneq ($(wildcard $(EMSDK)/upstream/emscripten/emcc),)
BROWSER_PATH := $(EMSDK)/upstream/emscripten:$(PATH)
else
BROWSER_PATH := $(PATH)
endif
WASM_UPY := $(TOP)/ports/webassembly/build-metal/micropython.mjs
WS ?= $(abspath $(TOP)/../..)
VSCODE_CDB ?= $(WS)/.vscode/compile_commands.json

.PHONY: test prove-all clean compile-commands gen metal-lib upy browser firmware firmware-prove firmware-check menu help menu-list FORCE

FORCE:

help menu-list:
	bash $(CURDIR)/menu.sh list

menu:
	bash $(CURDIR)/menu.sh

metal-lib: $(METAL_STATICLIB)

$(METAL_STATICLIB): FORCE
	cd $(CURDIR) && cargo build --lib --release --no-default-features --features upy-host

gen:
	$(WASMMOD)/tools/genfaces.sh --force $(METAL_SRC)

$(CURDIR)/build/mbedtls/%.o: $(MBEDTLS_DIR)/library/%.c
	mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O1 -g $(MBEDTLS_CPPFLAGS) -c -o $@ $<

$(OUT): $(SRCS) $(MBEDTLS_OBJS) $(METAL_STATICLIB)
	mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $(OUT) $(SRCS) $(MBEDTLS_OBJS) $(LDFLAGS_WASMMOD)

test: prove-all

prove-all: $(OUT) firmware-check
	$(OUT)
	$(MAKE) firmware-prove
	$(MAKE) upy
	$(MAKE) browser

firmware:
	$(MAKE) -C $(CURDIR)/port BOARD=X86_64_BIOS all
	$(MAKE) -C $(CURDIR)/port BOARD=X86_64_UEFI all
	$(MAKE) -C $(CURDIR)/port BOARD=ARMV7_RV1106 all

firmware-prove:
	$(MAKE) -C $(CURDIR)/port BOARD=X86_64_BIOS prove
	$(MAKE) -C $(CURDIR)/port BOARD=X86_64_UEFI prove
	$(MAKE) -C $(CURDIR)/port BOARD=ARMV7_RV1106 prove

firmware-check:
	mkdir -p $(CURDIR)/build
	$(CC) -std=gnu11 -Wall -Wextra -Werror -c -o $(CURDIR)/build/firmware_check.o \
		-I$(CURDIR) -I$(TOP) $(CURDIR)/firmware_check.c
	@echo firmware Metal GC/scheduler off ok

upy:
	$(MAKE) -C $(TOP)/ports/unix MICROPY_PY_WASM=1 MICROPY_PY_METAL=1 BUILD=build-metal
	$(TOP)/ports/unix/build-metal/micropython $(CURDIR)/upy_guest_prove.py
	python3 $(CURDIR)/upy_cdn_prove_host.py \
		$(TOP)/ports/unix/build-metal/micropython $(CURDIR)/upy_cdn_prove.py

browser:
	mkdir -p $(CURDIR)/build
	PATH="$(BROWSER_PATH)" bash -c 'command -v emcc >/dev/null || { echo "emcc not on PATH; set EMSDK to an emsdk checkout" >&2; exit 1; }'
	PATH="$(BROWSER_PATH)" \
		$(MAKE) -C $(TOP)/ports/webassembly MICROPY_PY_WASM=1 MICROPY_PY_METAL=1 BUILD=build-metal
	$(NODE) $(CURDIR)/upy_browser_prove.mjs $(WASM_UPY) $(CURDIR)/upy_browser_prove.py \
		> $(CURDIR)/build/browser_prove.log 2>&1
	cat $(CURDIR)/build/browser_prove.log
	grep -q "upy cdn js.fetch" $(CURDIR)/build/browser_prove.log
	grep -q "upy pack import" $(CURDIR)/build/browser_prove.log
	grep -q "upy metal ready" $(CURDIR)/build/browser_prove.log
	grep -q "upy native card import" $(CURDIR)/build/browser_prove.log

compile-commands:
	python3 $(WASMMOD)/compile_commands.py $(WASMMOD) $(WS) $(VSCODE_CDB)
	python3 $(CURDIR)/compile_commands.py $(CURDIR) $(TOP) $(WASMMOD) $(WS) $(VSCODE_CDB)

clean:
	rm -rf $(CURDIR)/build
