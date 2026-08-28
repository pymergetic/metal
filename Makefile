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
ZENOH_PICO_DIR ?= $(CURDIR)/externals/zenoh-pico
# zenoh-pico generic/Metal compile-time config + platform header live in the
# net.zenoh card dir (never in the vendored tree); the card dir must be an
# include path for the GENERIC branch of zenoh-pico's config.h / platform.h.
ZENOH_CARD_DIR ?= $(METAL_SRC)/pymergetic/metal/net/zenoh

METAL_LIBDIR := $(shell cd $(CURDIR) && cargo metadata --no-deps --format-version 1 2>/dev/null | python3 -c "import sys,json; print(json.load(sys.stdin)['target_directory']+'/release')")
ifeq ($(METAL_LIBDIR),)
METAL_LIBDIR := $(CURDIR)/target/release
endif
METAL_STATICLIB := $(METAL_LIBDIR)/libpymergetic_metal.a
WASMMOD_IWASM_A := $(firstword $(wildcard $(METAL_LIBDIR)/build/*/out/vmlib/build/libiwasm.a))

CC ?= cc
CXX ?= g++
NODE ?= node
CFLAGS ?= -std=gnu11 -Wall -Wextra -Werror -O1 -g -pthread
CPPFLAGS += -I$(CURDIR)/host_inc -I$(METAL_SRC) -I$(WASMMOD_SRC) -I$(WASMMOD) -I$(TOP) \
	-I$(TOP)/ports/unix -I$(MBEDTLS_DIR)/include \
	-I$(ZENOH_PICO_DIR)/include -I$(ZENOH_PICO_DIR)/src -I$(ZENOH_CARD_DIR) \
	-D_POSIX_C_SOURCE=200809L -DPM_WASMMOD_GUEST=0 -DPM_MOD_TESTS=1 \
	-DPM_MOD_BENCHES=1 -DZENOH_GENERIC \
	-DMICROPY_SSL_MBEDTLS=1 \
	-DMBEDTLS_CONFIG_FILE='"mbedtls/mbedtls_config_port.h"'

WASMMOD_GEN_ROOTS := $(CURDIR)/src
include $(WASMMOD)/gen.mk
include $(CURDIR)/tools/www.mk
include $(CURDIR)/tools/src.mk
include $(CURDIR)/tools/ledger.mk

# Cards and their tests come from the tree (tools/cards.sh), so a new card is
# proven here the moment it has a manifest — nothing to add below.
CARD_REL := $(shell $(CURDIR)/tools/cards.sh impl $(METAL_SRC)/pymergetic/metal) \
	$(shell $(CURDIR)/tools/cards.sh tests $(METAL_SRC)/pymergetic/metal)
ifeq ($(CARD_REL),)
$(error metal card discovery failed — see tools/cards.sh output above)
endif

SRCS := host_test.c \
	$(addprefix $(METAL_SRC)/pymergetic/metal/,$(CARD_REL))

# Compile C sources to individual objects so the final link can use g++
# (cc can't resolve C++ ABI symbols from the mrustc embed shim).
SRC_OBJS := $(addprefix $(CURDIR)/build/, $(SRCS:.c=.o))
BENCH_SRC_OBJS := $(addprefix $(CURDIR)/build/, $(BENCH_SRCS:.c=.o))

# The generated embed headers (src/www/ledger bytes) are #include'd by card
# objects; a regeneration without a recompile leaves stale bytes in the .o.
# Every card object depends on them — embed_src.py leaves the file untouched
# when bytes match, so this only rebuilds on a real content change.
$(SRC_OBJS) $(BENCH_SRC_OBJS): $(PM_METAL_SRC_INC) $(PM_METAL_WWW_INC) $(PM_METAL_LEDGER_INC)

# Bench binary reuses the same cards (incl. __bench__.c, which is inert under
# the test runner) but swaps the entrypoint. Benches report numbers and never
# gate, so both binaries register them into the same registry.
BENCH_SRCS := host_bench.c \
	$(addprefix $(METAL_SRC)/pymergetic/metal/,$(CARD_REL))

# -rdynamic: the build card's process resolver (dlsym on dlopen(NULL)) must
# find the pre-linked card symbols (tcc_new, pm_util_mem_alloc, ...) in the
# main binary's dynamic symbol table.
LDFLAGS_WASMMOD := -rdynamic -L$(METAL_LIBDIR) -lpymergetic_metal -lpthread -ldl -lm -lstdc++ -lrt

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

# zenoh-pico: vendored freestanding core + the card's own platform shim. The
# card border (__impl__.c, platform_metal.c, platform_metal_sys.c) is picked up
# by cards.sh impl from the manifest; only the vendored core is listed here,
# mbedtls-style, from the shared source glob (tools/zenoh.mk — same list the
# firmware seats use, compiled in GENERIC mode so it uses ZENOH_CARD_DIR's
# zenoh_generic_{config,platform}.h instead of a board system layer).
include $(CURDIR)/tools/zenoh.mk
ZP_OBJS := $(addprefix $(CURDIR)/build/zenoh-pico/,$(sort $(ZP_REL:.c=.o)))
ZP_CPPFLAGS := -DZENOH_GENERIC -I$(ZENOH_PICO_DIR)/include -I$(ZENOH_PICO_DIR)/src \
	-I$(ZENOH_CARD_DIR) -D_POSIX_C_SOURCE=200809L

# vendored TCC (externals/tcc) — compiled from source, one TU. The manifest's
# sources are the translation set libtcc.c #includes (ONE_SOURCE default 1), so
# they are the object's dependency list. Compiling them as separate objects
# would need tcc.c (the CLI driver), which no seat links.
include $(CURDIR)/tools/tcc.mk
TCC_DIR ?= $(CURDIR)/externals/tcc
TCC_OBJS := $(CURDIR)/build/tcc/libtcc.o
TCC_DEPS := $(addprefix $(TCC_DIR)/,$(TCC_MANIFEST_SRCS))
CPPFLAGS += -DTCC_TARGET_X86_64 -DPM_HAS_TCC=1 -I$(TCC_DIR) -DPM_METAL_TCC_LIB_DIR=\"$(TCC_DIR)\"

# In-tree ELF64 ET_REL relocator (wasmmod) — the build card's multi-object
# link drives it. Host seat only: the browser cell has no ELF loader
# (MICROPY_PY_WASM_ELF=0 there) and firmware links none either.
ELF_LOAD_SRC := $(WASMMOD_SRC)/pymergetic/wasmmod/pack/format/elf/load.c
ELF_LOAD_OBJ := $(CURDIR)/build/elf/load.o
CPPFLAGS += -DPM_METAL_BUILD_HAS_ELF=1
$(ELF_LOAD_OBJ): $(ELF_LOAD_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(WASMMOD_SRC) -I$(WASMMOD) -DMICROPY_PY_WASM_ELF=1 -c -o $@ $<
# vendored mrustc (externals/mrustc) — in-process Rust→C JIT front-end. The card's
# __impl__.c calls pm_metal_jit_rs_mrustc_compile() which is provided by the
# tools/mrustc_embed C++ shim; the shim reproduces mrustc's CLI driver pipeline
# (every pass is in bin/mrustc.a; only CLI main.o is excluded) and runs it in
# this process — no subprocess, no system().
MRUSTC_DIR ?= $(CURDIR)/externals/mrustc
MRUSTC_A := $(MRUSTC_DIR)/bin/mrustc.a
MRUSTC_COMMON_A := $(MRUSTC_DIR)/bin/common_lib.a
MRUSTC_EMBED_DIR := $(CURDIR)/tools/mrustc_embed
MRUSTC_EMBED_O := $(CURDIR)/build/mrustc_embed.o
MRUSTC_CXXFLAGS := -std=c++14 -O2 -g -Wall -Wno-unused-parameter -Wno-sign-compare \
	-I $(MRUSTC_DIR)/src/include -I $(MRUSTC_DIR)/src -I $(MRUSTC_DIR)/tools/common \
	-I $(MRUSTC_EMBED_DIR)
CPPFLAGS += -DPM_HAS_MRUSTC=1 -I$(MRUSTC_EMBED_DIR)

# mrustc.a must be whole-archive: the embed shim is the only direct client of
# the pass objects inside, so the linker would otherwise drop them all.
LDFLAGS_MRUSTC := -Wl,--whole-archive $(MRUSTC_A) -Wl,--no-whole-archive $(MRUSTC_COMMON_A) -lz

$(CURDIR)/build/zenoh-pico/%.o: $(ZENOH_PICO_DIR)/%.c
	mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O1 -g -Wall -Wextra $(ZP_CPPFLAGS) -c -o $@ $<

$(CURDIR)/build/tcc/libtcc.o: $(TCC_DEPS)
	mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O1 -g -Wall -Wno-unused-parameter -Wno-sign-compare \
		-I$(TCC_DIR) -DTCC_TARGET_X86_64 $(TCC_DEFINES) -c -o $@ $(TCC_DIR)/libtcc.c

# mrustc in-process embed shim: compile the C++ shim that drives mrustc.a
$(MRUSTC_EMBED_O): $(MRUSTC_EMBED_DIR)/mrustc_embed.cpp $(MRUSTC_EMBED_DIR)/mrustc_embed.h $(MRUSTC_A) $(MRUSTC_COMMON_A)
	mkdir -p $(dir $@)
	$(CXX) $(MRUSTC_CXXFLAGS) -c -o $@ $<

OUT := $(CURDIR)/build/metal-async-test
BENCH_OUT := $(CURDIR)/build/metal-async-bench
# emcc from PATH, or $EMSDK/upstream/emscripten — do not bake a home directory.
ifneq ($(wildcard $(EMSDK)/upstream/emscripten/emcc),)
BROWSER_PATH := $(EMSDK)/upstream/emscripten:$(PATH)
else
BROWSER_PATH := $(PATH)
endif
WASM_UPY := $(TOP)/ports/webassembly/build-metal/micropython.mjs
WS ?= $(abspath $(TOP)/../..)
VSCODE_CDB ?= $(WS)/.vscode/compile_commands.json

.PHONY: test bench prove-all clean compile-commands gen metal-lib upy browser firmware firmware-prove firmware-check menu help menu-list FORCE prove-zpico

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

# Compile every C source to an object in build/ mirroring the source tree.
$(CURDIR)/build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(OUT): $(SRC_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(METAL_STATICLIB)
	@mkdir -p $(dir $(OUT))
	$(CXX) -o $(OUT) $(SRC_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(LDFLAGS_WASMMOD) $(LDFLAGS_MRUSTC)

$(BENCH_OUT): $(BENCH_SRC_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(METAL_STATICLIB)
	@mkdir -p $(dir $(BENCH_OUT))
	$(CXX) -o $(BENCH_OUT) $(BENCH_SRC_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(LDFLAGS_WASMMOD) $(LDFLAGS_MRUSTC)

test: prove-all

# WASM32 backend prove (standalone)
wasm32-prove:
	mkdir -p $(CURDIR)/build
	$(CC) -std=gnu11 -O1 -g -I$(TCC_DIR) -DTCC_TARGET_WASM32 -DONE_SOURCE \
		$(TCC_DIR)/wasm32_prove.c $(TCC_DIR)/libtcc.c \
		-lm -ldl -o $(CURDIR)/build/wasm32_prove && $(CURDIR)/build/wasm32_prove

test: prove-all

# Benches report numbers and never gate. `make bench` builds + runs the
# registry-walking runner; the result is human guidance, not a CI gate.
bench: $(BENCH_OUT)
	$(BENCH_OUT)

# Stage 1 standalone prove: the vendored zenoh-pico lib builds and delivers
# unicast pub/sub on lo by itself, before any Metal card wraps it.
prove-zpico:
	$(MAKE) -C $(CURDIR)/tools/zp_pico_prove prove

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
	mkdir -p $(CURDIR)/build
	$(MAKE) -C $(TOP)/ports/unix MICROPY_PY_WASM=1 MICROPY_PY_METAL=1 MICROPY_PY_THREAD_GIL=1 BUILD=build-metal LDFLAGS_EXTRA="-Wl,-no-pie -rdynamic"
	$(TOP)/ports/unix/build-metal/micropython $(CURDIR)/upy_guest_prove.py
	$(TOP)/ports/unix/build-metal/micropython $(CURDIR)/upy_runner_vm_prove.py
	python3 $(CURDIR)/upy_cdn_prove_host.py \
		$(TOP)/ports/unix/build-metal/micropython $(CURDIR)/upy_cdn_prove.py
	$(TOP)/ports/unix/build-metal/micropython $(CURDIR)/upy_shutdown_prove.py \
		> $(CURDIR)/build/upy_shutdown.log 2>&1
	grep -q "upy shutdown prove" $(CURDIR)/build/upy_shutdown.log
	grep -qE "stop .*card\(s\)" $(CURDIR)/build/upy_shutdown.log
	! grep -q "nothing booted" $(CURDIR)/build/upy_shutdown.log
	@echo upy shutdown unwound the boot graph ok

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
	grep -q "upy inspect" $(CURDIR)/build/browser_prove.log
	grep -q "upy inspect caps" $(CURDIR)/build/browser_prove.log
	grep -q "upy src" $(CURDIR)/build/browser_prove.log
	grep -q "upy dns" $(CURDIR)/build/browser_prove.log
	grep -q "upy socket" $(CURDIR)/build/browser_prove.log
	grep -q "upy display present" $(CURDIR)/build/browser_prove.log
	grep -q "upy input feed" $(CURDIR)/build/browser_prove.log
	grep -q "upy console ids" $(CURDIR)/build/browser_prove.log
	grep -q "upy fs embed" $(CURDIR)/build/browser_prove.log
	grep -q "upy ledger round-trip" $(CURDIR)/build/browser_prove.log
	grep -q "upy accessor spine" $(CURDIR)/build/browser_prove.log
	grep -q "upy editor" $(CURDIR)/build/browser_prove.log
	grep -q "upy process" $(CURDIR)/build/browser_prove.log
	grep -q "upy ssh session" $(CURDIR)/build/browser_prove.log
	grep -q "upy zenoh" $(CURDIR)/build/browser_prove.log
	grep -q "upy swarm" $(CURDIR)/build/browser_prove.log

compile-commands:
	python3 $(WASMMOD)/compile_commands.py $(WASMMOD) $(WS) $(VSCODE_CDB)
	python3 $(CURDIR)/compile_commands.py $(CURDIR) $(TOP) $(WASMMOD) $(WS) $(VSCODE_CDB)

clean:
	rm -rf $(CURDIR)/build
