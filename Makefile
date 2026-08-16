# Metal host prove (no µPy). Heap/lock are wasmmod: util.mem (impl=c) and
# util.lock (impl=rs) from libpymergetic_wasmmod.a — not a C lock twin.
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

WASMMOD_LIBDIR := $(shell cd $(WASMMOD) && cargo metadata --no-deps --format-version 1 2>/dev/null | python3 -c "import sys,json; print(json.load(sys.stdin)['target_directory']+'/release')")
ifeq ($(WASMMOD_LIBDIR),)
WASMMOD_LIBDIR := $(WASMMOD)/target/release
endif
WASMMOD_STATICLIB := $(WASMMOD_LIBDIR)/libpymergetic_wasmmod.a
WASMMOD_IWASM_A := $(firstword $(wildcard $(WASMMOD_LIBDIR)/build/*/out/vmlib/build/libiwasm.a))

CC ?= cc
NODE ?= node
CFLAGS ?= -std=gnu11 -Wall -Wextra -Werror -O1 -g
CPPFLAGS += -I$(CURDIR)/host_inc -I$(METAL_SRC) -I$(WASMMOD_SRC) -I$(WASMMOD) -I$(TOP) \
	-I$(TOP)/ports/unix -I$(MBEDTLS_DIR)/include \
	-D_POSIX_C_SOURCE=200809L -DPM_WASMMOD_GUEST=1 -DMICROPY_SSL_MBEDTLS=1 \
	-DMBEDTLS_CONFIG_FILE='"mbedtls/mbedtls_config_port.h"'

SRCS := \
	host_test.c \
	$(METAL_SRC)/pymergetic/metal/dt/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/dt/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/bus/pci/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/bus/pci/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/bus/virtio/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/bus/virtio/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/net/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/net/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/blk/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/blk/virtio/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/blk/virtio/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/blk/ide/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/blk/ide/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/rtc/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/rtc/sim/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/rtc/sim/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/rtc/cmos/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/rtc/cmos/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/fw/memmap/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/fw/memmap/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/async/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/async/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/net/ip/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/net/ip/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/net/tls/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/net/tls/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/net/http/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/net/http/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/net/http/asgi/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/net/tap/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/net/tap/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/net/dns/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/net/dns/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/net/sim/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/net/sim/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/net/virtio/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/net/virtio/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/net/wg/__crypto__.c \
	$(METAL_SRC)/pymergetic/metal/net/wg/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/net/wg/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/net/bge/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/drivers/net/bge/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/net/ntp/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/net/ntp/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/net/dhcp/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/net/dhcp/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/net/tftp/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/net/tftp/__tests__.c \
	$(METAL_SRC)/pymergetic/metal/net/ssh/__impl__.c \
	$(METAL_SRC)/pymergetic/metal/net/ssh/__tests__.c

LDFLAGS_WASMMOD := -L$(WASMMOD_LIBDIR) -lpymergetic_wasmmod -lpthread -ldl -lm -lstdc++ -lrt
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
EMSDK ?= /home/ladmin/emsdk
WASM_UPY := $(TOP)/ports/webassembly/build-metal/micropython.mjs
WS ?= $(abspath $(TOP)/../..)
VSCODE_CDB ?= $(WS)/.vscode/compile_commands.json

.PHONY: test clean compile-commands gen wasmmod-lib upy browser firmware firmware-check FORCE

FORCE:

wasmmod-lib: $(WASMMOD_STATICLIB)

$(WASMMOD_STATICLIB): FORCE
	cd $(WASMMOD) && cargo build --lib --release --no-default-features --features upy-host

gen:
	cd $(WASMMOD) && cargo run --features gen --bin wasmmod-gen

$(CURDIR)/build/mbedtls/%.o: $(MBEDTLS_DIR)/library/%.c
	mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O1 -g $(MBEDTLS_CPPFLAGS) -c -o $@ $<

$(OUT): $(SRCS) $(MBEDTLS_OBJS) $(WASMMOD_STATICLIB)
	mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $(OUT) $(SRCS) $(MBEDTLS_OBJS) $(LDFLAGS_WASMMOD)

test: $(OUT) firmware-check
	$(OUT)
	$(MAKE) firmware
	$(MAKE) upy
	$(MAKE) browser

firmware:
	$(MAKE) -C $(CURDIR)/port BOARD=X86_64_BIOS run
	$(MAKE) -C $(CURDIR)/port BOARD=X86_64_UEFI run

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
	PATH="$(EMSDK)/upstream/emscripten:$(PATH)" \
		$(MAKE) -C $(TOP)/ports/webassembly MICROPY_PY_WASM=1 MICROPY_PY_METAL=1 BUILD=build-metal
	$(NODE) $(CURDIR)/upy_browser_prove.mjs $(WASM_UPY) $(CURDIR)/upy_browser_prove.py \
		> $(CURDIR)/build/browser_prove.log 2>&1
	cat $(CURDIR)/build/browser_prove.log
	grep -q "upy cdn js.fetch" $(CURDIR)/build/browser_prove.log
	grep -q "upy pack import" $(CURDIR)/build/browser_prove.log
	grep -q "upy metal ready" $(CURDIR)/build/browser_prove.log

compile-commands:
	python3 $(WASMMOD)/compile_commands.py $(WASMMOD) $(WS) $(VSCODE_CDB)
	python3 $(CURDIR)/compile_commands.py $(CURDIR) $(TOP) $(WASMMOD) $(WS) $(VSCODE_CDB)

clean:
	rm -rf $(CURDIR)/build
