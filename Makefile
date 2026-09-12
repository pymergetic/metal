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
# Header dependencies, the same posture every board build.mk already takes.
# A separate += rather than part of the ?= above: an environment CFLAGS must
# not be able to switch dependency tracking off. The .d files land beside the
# .o they describe and are read back at the foot of this file.
CFLAGS += -MMD -MP
# host_upy first: py/mpconfig.h's <mpconfigport.h> must resolve to the host
# seat's embedded µPy config (host_upy/), never to ports/unix/ (whose variant
# include the metal build cannot satisfy). ports/embed is the config's
# <port/mpconfigport_common.h> home. $(TOP) precedes host_inc so the REAL
# py/mpconfig.h wins for host-cc TUs that include µPy headers (jit/py);
# host_inc's py/mpconfig.h stub only serves ksweep's TCC include list,
# which has no host_upy/TOP and needs the two mbedtls carrier macros.
CPPFLAGS += -I$(CURDIR)/host_upy -I$(TOP)/ports/embed
CPPFLAGS += -I$(TOP) -I$(CURDIR)/host_inc -I$(METAL_SRC) -I$(WASMMOD_SRC) -I$(WASMMOD) \
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
include $(CURDIR)/tools/tccsrc.mk

# The host seat's embedded µPy kernel (ports/embed route — see
# tools/upy_embed.mk): backs pymergetic.metal.jit.py's object loop with a
# real in-process compile instead of the no-µPy refusal. MICROPY_PY_WASM=1
# turns the card's µPy paths on (only jit/py/__impl__.c reads it among the
# linked sources), PM_METAL_JIT_PY_EMBED=1 marks THIS binary as the seat
# that owns the kernel lifecycle (the µPy seats' ports init µPy themselves,
# so the card's boot init must stay a no-op there).
include $(CURDIR)/tools/upy_embed.mk
UPY_EMBED_LATE := upy-embed
CPPFLAGS += -DMICROPY_PY_WASM=1 -DMICROPY_PERSISTENT_CODE_SAVE=1 -DPM_METAL_JIT_PY_EMBED=1

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
# Order-only: the embedded µPy package must exist (its genhdr/ feeds
# py/qstr.h in every card TU that includes µPy headers) before card objects
# compile — without rebuilding cards when only embed internals change.
$(SRC_OBJS) $(BENCH_SRC_OBJS): $(PM_METAL_SRC_INC) $(PM_METAL_WWW_INC) $(PM_METAL_LEDGER_INC) $(CURDIR)/host_upy/mpconfigport.h | $(UPY_EMBED_STAMP)
CPPFLAGS += -I$(UPY_EMBED_PACKAGE)

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
include $(CURDIR)/tools/tcc_instances.mk
TCC_DIR ?= $(CURDIR)/externals/tcc
TCC_DEPS := $(addprefix $(TCC_DIR)/,$(TCC_MANIFEST_SRCS))
# Host seat instances (one recipe, N instances — tools/tcc_instances.mk):
# native x86_64 plus both cross backends, so this binary can compile for
# every arch. Cross objects rename every defined global (tcc_prefix_syms.sh)
# so the three instances link side by side. TCC_OBJS feeds the same link
# lines TCC_CROSS_OBJS already feeds — keep them disjoint.
TCC_OBJS := $(CURDIR)/build/tcc/libtcc.o
TCC_CROSS_OBJS := $(CURDIR)/build/tcc/libtcc_wasm_cross.o $(CURDIR)/build/tcc/libtcc_arm_cross.o
$(eval $(call tcc_instance,x86_64,TCC_TARGET_X86_64,$(CURDIR)/build/tcc/libtcc.o,))
$(eval $(call tcc_instance,wasm32_cross,TCC_TARGET_WASM32,$(CURDIR)/build/tcc/libtcc_wasm_cross.o,pm_tccw_))
$(eval $(call tcc_instance,arm_eabi_cross,TCC_TARGET_ARM,$(CURDIR)/build/tcc/libtcc_arm_cross.o,pm_tcca_))
# TCC runtime helpers (libtcc1 pieces): cards compiled in-kernel call these
# (__va_arg from va_arg lowering, __atomic_* from _Atomic/__atomic_*).
# The host seat binary defines them so the build card's process resolver
# (dlsym, -rdynamic) answers them for every in-kernel link — the same
# posture as libgcc's __floatundixf shims in the build card's tests.
TCC1_OBJS := $(CURDIR)/build/tcc/libtcc1.o $(CURDIR)/build/tcc/libtcc1_atomic.o \
	$(CURDIR)/build/tcc/libtcc1_stdatomic.o
TCC_DEPS := $(addprefix $(TCC_DIR)/,$(TCC_MANIFEST_SRCS))
CPPFLAGS += -DTCC_TARGET_X86_64 -DPM_HAS_TCC=1 -I$(TCC_DIR) -DPM_METAL_TCC_LIB_DIR=\"$(TCC_DIR)\"
CPPFLAGS += -DPM_METAL_TCC_CROSS_WASM32=1
CPPFLAGS += -DPM_METAL_TCC_CROSS_ARM_EABI=1
# Absolute tree roots for the runtime build faces (inspect's /build rebuild
# route, ksweep): __FILE__ is relative under make, so a route serving a
# rebuild from any CWD needs the absolute anchors. Same pattern as
# PM_METAL_TCC_LIB_DIR above.
CPPFLAGS += -DPM_METAL_ROOT=\"$(CURDIR)\" -DPM_METAL_WASMMOD_ROOT=\"$(abspath ../wasmmod)\"

# In-tree ELF64 ET_REL relocator (wasmmod) — the build card's multi-object
# link drives it. Host seat only: the browser cell has no ELF loader
# (MICROPY_PY_WASM_ELF=0 there) and firmware links none either.
ELF_LOAD_SRC := $(WASMMOD_SRC)/pymergetic/wasmmod/pack/format/elf/load.c
ELF_LOAD_OBJ := $(CURDIR)/build/elf/load.o
CPPFLAGS += -DPM_METAL_BUILD_HAS_ELF=1
$(ELF_LOAD_OBJ): $(ELF_LOAD_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(WASMMOD_SRC) -I$(WASMMOD) -DMICROPY_PY_WASM_ELF=1 -c -o $@ $<
# wasmmod C-card test objects — compiled here (not pulled from the static
# archive, whose unreferenced __tests__ members the final link drops; same
# posture as ELF_LOAD_OBJ above) so the widened host runner (host_test.c
# walks every registered module, metal and wasmmod alike) executes them on
# every `make test`. PM_MOD_TESTS is the guest.h gate that turns
# PM_MOD_TEST_C into a real registration.
WASMMOD_TESTS_OBJ := \
	$(CURDIR)/build/wasmmod-tests/types.o \
	$(CURDIR)/build/wasmmod-tests/io.o \
	$(CURDIR)/build/wasmmod-tests/net-cdn.o \
	$(CURDIR)/build/wasmmod-tests/nativecall.o \
	$(CURDIR)/build/wasmmod-tests/util-limits.o

$(CURDIR)/build/wasmmod-tests/types.o: $(WASMMOD_SRC)/pymergetic/types/__tests__.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(WASMMOD_SRC) -I$(WASMMOD) -DPM_MOD_TESTS=1 -c -o $@ $<

$(CURDIR)/build/wasmmod-tests/io.o: $(WASMMOD_SRC)/pymergetic/wasmmod/io/__tests__.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(WASMMOD_SRC) -I$(WASMMOD) -DPM_MOD_TESTS=1 -c -o $@ $<

$(CURDIR)/build/wasmmod-tests/util-limits.o: $(WASMMOD_SRC)/pymergetic/util/limits/__tests__.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(WASMMOD_SRC) -I$(WASMMOD) -DPM_MOD_TESTS=1 -c -o $@ $<

$(CURDIR)/build/wasmmod-tests/net-cdn.o: $(WASMMOD_SRC)/pymergetic/wasmmod/net/cdn/__tests__.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(WASMMOD_SRC) -I$(WASMMOD) -DPM_MOD_TESTS=1 -c -o $@ $<

$(CURDIR)/build/wasmmod-tests/nativecall.o: $(WASMMOD_SRC)/pymergetic/wasmmod/nativecall/__tests__.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(WASMMOD_SRC) -I$(WASMMOD) -DPM_MOD_TESTS=1 -c -o $@ $<

# The knob faces from C++ — the language the transpile chain and the mrustc shim
# are written in. A consumer TU, compiled by $(CXX) and linked into the host
# runner, so "reachable from C++" is a link and a run rather than a claim.
LIMITS_CPP_PROVE_O := $(CURDIR)/build/host_test_cpp.o
$(LIMITS_CPP_PROVE_O): $(CURDIR)/host_test_cpp.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++17 -O2 -g -Wall -Wextra -MMD -MP $(CPPFLAGS) -c -o $@ $<

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
	$(CC) -std=gnu11 -O1 -g -Wall -Wextra -MMD -MP $(ZP_CPPFLAGS) -c -o $@ $<

# tcc1 runtime: va_list.c defines __va_arg (and friends) in portable C;
# atomic.S is hand-written x86_64 asm for the __atomic_* family.
$(CURDIR)/build/tcc/libtcc1.o: $(TCC_DIR)/lib/va_list.c
	mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O1 -g -w -MMD -MP -I$(TCC_DIR) -I$(TCC_DIR)/lib \
		-c -o $@ $(TCC_DIR)/lib/va_list.c

$(CURDIR)/build/tcc/libtcc1_atomic.o: $(TCC_DIR)/lib/atomic.S
	mkdir -p $(dir $@)
	$(CC) -I$(TCC_DIR)/lib -c -o $@ $(TCC_DIR)/lib/atomic.S

# stdatomic.c instantiates the __atomic_fetch_* / *_fetch families
# (generated from one macro) on top of the load/store/cmpxchg primitives.
$(CURDIR)/build/tcc/libtcc1_stdatomic.o: $(TCC_DIR)/lib/stdatomic.c
	mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O1 -g -w -MMD -MP -c -o $@ $(TCC_DIR)/lib/stdatomic.c

# cross instances (host seat): wasm32 + arm-eabi backends, symbols renamed by
# tools/tcc_instances.mk / tcc_prefix_syms.sh (rename ALL defined globals —
# the old tcc_*/wasm_ grep leaked gen_negf, which both backends define).
# jit.c's cross path declares the prefixed names by hand — each instance's
# TCCState layout differs from the native one, so only opaque-pointer calls
# cross that seam.

# mrustc in-process embed shim: compile the C++ shim that drives mrustc.a
$(MRUSTC_EMBED_O): $(MRUSTC_EMBED_DIR)/mrustc_embed.cpp $(MRUSTC_EMBED_DIR)/mrustc_embed.h $(MRUSTC_A) $(MRUSTC_COMMON_A)
	mkdir -p $(dir $@)
	$(CXX) $(MRUSTC_CXXFLAGS) -c -o $@ $<

OUT := $(CURDIR)/build/metal-async-test
BENCH_OUT := $(CURDIR)/build/metal-async-bench
# selfhost feed: the developer driver for the micro-rustc self-host cycle
# (tools/selfhost_cycle.sh). Same card/link config as the host prove so the
# build card's ELF link path is in-process here too — the feed's --link mode
# is what closes the Rust -> C -> TCC -> link loop with no host cc.
SELFHOST_FEED := $(CURDIR)/build/selfhost_feed
# emcc from PATH, or $EMSDK/upstream/emscripten — do not bake a home directory.
ifneq ($(wildcard $(EMSDK)/upstream/emscripten/emcc),)
BROWSER_PATH := $(EMSDK)/upstream/emscripten:$(PATH)
else
BROWSER_PATH := $(PATH)
endif
WASM_UPY := $(TOP)/ports/webassembly/build-metal/micropython.mjs
WS ?= $(abspath $(TOP)/../..)
VSCODE_CDB ?= $(WS)/.vscode/compile_commands.json

.PHONY: test bench prove-all clean compile-commands gen metal-lib upy browser firmware firmware-prove firmware-check menu help menu-list FORCE prove-zpico selfhost ksweep rsx-probe rsx-dump rsx-hwm rsx-span

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
	$(CC) -std=gnu11 -O1 -g -MMD -MP $(MBEDTLS_CPPFLAGS) -c -o $@ $<

# Compile every C source to an object in build/ mirroring the source tree.
$(CURDIR)/build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# jit/py embeds the upy faces and includes upstream upy headers (pystack.h's
# mp_nonlocal_realloc inline has an unused parameter in some configs) -- these
# two TUs drop -Werror for that upstream warning class only.
UPY_TU_CFLAGS = $(filter-out -Werror,$(CFLAGS)) -Wno-unused-parameter
$(CURDIR)/build/$(METAL_SRC)/pymergetic/metal/jit/py/__impl__.o: $(METAL_SRC)/pymergetic/metal/jit/py/__impl__.c
	@mkdir -p $(dir $@)
	$(CC) $(UPY_TU_CFLAGS) $(CPPFLAGS) -c -o $@ $<
$(CURDIR)/build/$(METAL_SRC)/pymergetic/metal/jit/py/__tests__.o: $(METAL_SRC)/pymergetic/metal/jit/py/__tests__.c
	@mkdir -p $(dir $@)
	$(CC) $(UPY_TU_CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(OUT): $(SRC_OBJS) $(WASMMOD_TESTS_OBJ) $(LIMITS_CPP_PROVE_O) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(UPY_EMBED_OBJS_FILE) $(METAL_STATICLIB)
	@mkdir -p $(dir $(OUT))
	$(CXX) -o $(OUT) $(SRC_OBJS) $(WASMMOD_TESTS_OBJ) $(LIMITS_CPP_PROVE_O) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(LDFLAGS_WASMMOD) $(LDFLAGS_MRUSTC) $(LDFLAGS_UPY)

$(BENCH_OUT): $(BENCH_SRC_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(UPY_EMBED_OBJS_FILE) $(METAL_STATICLIB)
	@mkdir -p $(dir $(BENCH_OUT))
	$(CXX) -o $(BENCH_OUT) $(BENCH_SRC_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(LDFLAGS_WASMMOD) $(LDFLAGS_MRUSTC) $(LDFLAGS_UPY)

SELFHOST_FEED_O := $(CURDIR)/build/selfhost_feed.o
$(SELFHOST_FEED_O): $(CURDIR)/tools/selfhost_feed.c
	@mkdir -p $(dir $(SELFHOST_FEED_O))
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# SRC_OBJS minus host_test.o (its main) — the feed brings its own main.
FEED_CARD_OBJS := $(filter-out $(CURDIR)/build/host_test.o,$(SRC_OBJS))

$(SELFHOST_FEED): $(SELFHOST_FEED_O) $(FEED_CARD_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(UPY_EMBED_OBJS_FILE) $(METAL_STATICLIB)
	@mkdir -p $(dir $(SELFHOST_FEED))
	$(CXX) -o $(SELFHOST_FEED) $(SELFHOST_FEED_O) $(FEED_CARD_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(LDFLAGS_WASMMOD) $(LDFLAGS_MRUSTC) $(LDFLAGS_UPY)

selfhost-feed: $(SELFHOST_FEED)

# ksweep: the in-kernel compile sweep driver (tools/ksweep.c). Same link
# config as the feed — every card + TCC + the ELF loader — because its job
# is to push every discovered unit through unit_compile (TCC objects ->
# relocator link) with the host seat's fill. Output is a readiness map
# (report at build/ksweep_report.txt), never a prove gate.
KSWEEP := $(CURDIR)/build/ksweep
KSWEEP_O := $(CURDIR)/build/ksweep.o

$(KSWEEP_O): $(CURDIR)/tools/ksweep.c
	@mkdir -p $(dir $(KSWEEP_O))
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(KSWEEP): $(KSWEEP_O) $(FEED_CARD_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(UPY_EMBED_OBJS_FILE) $(METAL_STATICLIB)
	@mkdir -p $(dir $(KSWEEP))
	$(CXX) -o $(KSWEEP) $(KSWEEP_O) $(FEED_CARD_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(LDFLAGS_WASMMOD) $(LDFLAGS_MRUSTC) $(LDFLAGS_UPY)

ksweep: $(KSWEEP)
	$(KSWEEP) $(CURDIR)/build/ksweep_report.txt

# rsx-probe: one-card diagnostic (tools/rsx_probe.c) — runs only the
# jit.rs.compiler tests entry and prints the raw rc per test. tools/
# posture: not a prove gate, same link shape as ksweep.
RSX_PROBE := $(CURDIR)/build/rsx_probe
RSX_PROBE_O := $(CURDIR)/build/rsx_probe.o

$(RSX_PROBE_O): $(CURDIR)/tools/rsx_probe.c
	@mkdir -p $(dir $(RSX_PROBE_O))
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(RSX_PROBE): $(RSX_PROBE_O) $(FEED_CARD_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(UPY_EMBED_OBJS_FILE) $(METAL_STATICLIB)
	@mkdir -p $(dir $(RSX_PROBE))
	$(CXX) -o $(RSX_PROBE) $(RSX_PROBE_O) $(FEED_CARD_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(LDFLAGS_WASMMOD) $(LDFLAGS_MRUSTC) $(LDFLAGS_UPY)

rsx-probe: $(RSX_PROBE)
	$(RSX_PROBE)

# rsx-dump: one-file C dump (tools/rsx_dump.c) — the generated C a card's
# Rust source lowers to, printed for diagnosis. Same tools/ posture and
# link shape as rsx_probe / ksweep; not a prove gate.
RSX_DUMP := $(CURDIR)/build/rsx_dump
RSX_DUMP_O := $(CURDIR)/build/rsx_dump.o

$(RSX_DUMP_O): $(CURDIR)/tools/rsx_dump.c
	@mkdir -p $(dir $(RSX_DUMP_O))
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(RSX_DUMP): $(RSX_DUMP_O) $(FEED_CARD_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(UPY_EMBED_OBJS_FILE) $(METAL_STATICLIB)
	@mkdir -p $(dir $(RSX_DUMP))
	$(CXX) -o $(RSX_DUMP) $(RSX_DUMP_O) $(FEED_CARD_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(LDFLAGS_WASMMOD) $(LDFLAGS_MRUSTC) $(LDFLAGS_UPY)

rsx-dump: $(RSX_DUMP)
	@echo "rsx-dump built: $(RSX_DUMP) <file.rs> [fqn...]"

# rsx-hwm: arena high-water audit (tools/rsx_hwm.c) — compiles 1/10/40/200-
# function sources and prints the heap draw per compile, proving the
# LocalTab-per-function cost is bounded and linear. tools/ posture: same
# link shape as rsx_probe / ksweep, not a prove gate.
RSX_HWM := $(CURDIR)/build/rsx_hwm
RSX_HWM_O := $(CURDIR)/build/rsx_hwm.o

$(RSX_HWM_O): $(CURDIR)/tools/rsx_hwm.c
	@mkdir -p $(dir $(RSX_HWM_O))
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(RSX_HWM): $(RSX_HWM_O) $(FEED_CARD_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(UPY_EMBED_OBJS_FILE) $(METAL_STATICLIB)
	@mkdir -p $(dir $(RSX_HWM))
	$(CXX) -o $(RSX_HWM) $(RSX_HWM_O) $(FEED_CARD_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(LDFLAGS_WASMMOD) $(LDFLAGS_MRUSTC) $(LDFLAGS_UPY)

rsx-hwm: $(RSX_HWM)
	$(RSX_HWM)

# rsx-span: same diagnostic shape as rsx_dump, but the arena span comes from
# argv — bisecting the draw a unit needs (the self_host prove's 64 MiB gate
# vs ksweep's 96 MiB per-unit arenas). Prints heap_used on success.
RSX_SPAN := $(CURDIR)/build/rsx_span
RSX_SPAN_O := $(CURDIR)/build/rsx_span.o

$(RSX_SPAN_O): $(CURDIR)/tools/rsx_span.c
	@mkdir -p $(dir $(RSX_SPAN_O))
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(RSX_SPAN): $(RSX_SPAN_O) $(FEED_CARD_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(UPY_EMBED_OBJS_FILE) $(METAL_STATICLIB)
	@mkdir -p $(dir $(RSX_SPAN))
	$(CXX) -o $(RSX_SPAN) $(RSX_SPAN_O) $(FEED_CARD_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(LDFLAGS_WASMMOD) $(LDFLAGS_MRUSTC) $(LDFLAGS_UPY)

rsx-span: $(RSX_SPAN)
	@echo "rsx-span built: $(RSX_SPAN) <file.rs> <span_mb>"

# selfhost self: the same feed entrypoint, but the rsx card's code comes from
# gen-1 C (micro-rustc's own output) instead of the rustc boot build — the
# self binary of tools/selfhost_cycle.sh stage 2. Every other card stays the
# boot build: the cycle isolates one card at a time. gen1.o rides the command
# line BEFORE the metal archive, so the archive's rsx member is never
# extracted (it defines rsx only — explicit objects win) and asgi + the rest
# of the crate still come from the archive.
SELFHOST_GEN1_C ?= /tmp/selfhost_cycle/gen1.c
SELFHOST_GEN1_O := $(CURDIR)/build/selfhost_gen1.o
SELFHOST_SELF := $(CURDIR)/build/selfhost_self

$(SELFHOST_GEN1_O): $(SELFHOST_GEN1_C)
	@mkdir -p $(dir $(SELFHOST_GEN1_O))
	$(CC) -std=gnu11 -O0 -g -w $(CPPFLAGS) -c -o $@ $<

$(SELFHOST_SELF): $(SELFHOST_FEED_O) $(SELFHOST_GEN1_O) $(FEED_CARD_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(UPY_EMBED_OBJS_FILE) $(METAL_STATICLIB)
	@mkdir -p $(dir $(SELFHOST_SELF))
	$(CXX) -o $(SELFHOST_SELF) $(SELFHOST_FEED_O) $(SELFHOST_GEN1_O) \
		$(FEED_CARD_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) \
		$(LDFLAGS_WASMMOD) $(LDFLAGS_MRUSTC) $(LDFLAGS_UPY)

selfhost-self: $(SELFHOST_SELF)

# cppx selfhost: the same cycle for the C++ card (tools/cppx_feed.c). The
# self binary swaps the jit.cpp card's object for the C the card generated
# from its own lowerer — every other card stays the boot build. cppx_gen1.o
# rides the command line BEFORE the metal archive so the archive's cppx
# member is never extracted (it defines cppx only — explicit objects win).
CPPX_FEED := $(CURDIR)/build/cppx_feed
CPPX_FEED_O := $(CURDIR)/build/cppx_feed.o
CPPX_GEN1_C ?= /tmp/selfhost_cycle/cppx_gen1.c
CPPX_GEN1_O := $(CURDIR)/build/cppx_gen1.o
CPPX_SELF := $(CURDIR)/build/cppx_self
# the cppx card is a C card: its boot object sits in FEED_CARD_OBJS, so the
# self link must drop it or the card's symbols double-define (gen1 wins by
# riding first; the rest of the card stack stays boot).
CPPX_CARD_OBJ := $(patsubst %.c,$(CURDIR)/build/%.o,$(abspath $(METAL_SRC)/pymergetic/metal/jit/cpp/__impl__.c))
CPPX_FEED_CARD_OBJS := $(filter-out $(CPPX_CARD_OBJ),$(FEED_CARD_OBJS))

$(CPPX_FEED_O): $(CURDIR)/tools/cppx_feed.c
	@mkdir -p $(dir $(CPPX_FEED_O))
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(CPPX_FEED): $(CPPX_FEED_O) $(FEED_CARD_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(UPY_EMBED_OBJS_FILE) $(METAL_STATICLIB)
	@mkdir -p $(dir $(CPPX_FEED))
	$(CXX) -o $(CPPX_FEED) $(CPPX_FEED_O) $(FEED_CARD_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(LDFLAGS_WASMMOD) $(LDFLAGS_MRUSTC) $(LDFLAGS_UPY)

cppx-feed: $(CPPX_FEED)

$(CPPX_GEN1_O): $(CPPX_GEN1_C)
	@mkdir -p $(dir $(CPPX_GEN1_O))
	$(CC) -std=gnu11 -O0 -g -w $(CPPFLAGS) -c -o $@ $<

$(CPPX_SELF): $(CPPX_FEED_O) $(CPPX_GEN1_O) $(CPPX_FEED_CARD_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) $(UPY_EMBED_OBJS_FILE) $(METAL_STATICLIB)
	@mkdir -p $(dir $(CPPX_SELF))
	$(CXX) -o $(CPPX_SELF) $(CPPX_FEED_O) $(CPPX_GEN1_O) \
		$(CPPX_FEED_CARD_OBJS) $(MBEDTLS_OBJS) $(ZP_OBJS) $(TCC_OBJS) $(TCC_CROSS_OBJS) $(TCC1_OBJS) $(MRUSTC_EMBED_O) $(ELF_LOAD_OBJ) \
		$(LDFLAGS_WASMMOD) $(LDFLAGS_MRUSTC) $(LDFLAGS_UPY)

cppx-self: $(CPPX_SELF)

# The whole self-host loop in one target: boot rebuild, rust fixed point,
# in-kernel object+link, corpus, and the C++ chain + its own selfhost —
# tools/selfhost_cycle.sh is the single source of truth for the stages.
selfhost:
	$(CURDIR)/tools/selfhost_cycle.sh

test: prove-all

# WASM32 backend prove (standalone)
wasm32-prove:
	mkdir -p $(CURDIR)/build
	$(CC) -std=gnu11 -O1 -g -I$(TCC_DIR) -DTCC_TARGET_WASM32 -DONE_SOURCE \
		-DPM_METAL_TCC_LIB_DIR=\"$(TCC_DIR)\" \
		$(TCC_DIR)/wasm32_prove.c $(TCC_DIR)/libtcc.c \
		-lm -ldl -o $(CURDIR)/build/wasm32_prove && $(CURDIR)/build/wasm32_prove

# Benches report numbers and never gate. `make bench` builds + runs the
# registry-walking runner; the result is human guidance, not a CI gate.
bench: $(BENCH_OUT)
	$(BENCH_OUT)

# Stage 1 standalone prove: the vendored zenoh-pico lib builds and delivers
# unicast pub/sub on lo by itself, before any Metal card wraps it.
prove-zpico:
	$(MAKE) -C $(CURDIR)/tools/zp_pico_prove prove

# wasm32-prove is in the gate because leaving it out is how the wasm32
# backend shipped a lane that emitted modules an engine loaded and computed
# the wrong answers from. It is cheap (one libtcc build) and it pins what
# that backend can and cannot lower.
prove-all: $(OUT) firmware-check
	$(OUT)
	$(MAKE) wasm32-prove
	$(MAKE) firmware-prove
	$(MAKE) upy
	$(MAKE) browser

firmware:
	$(MAKE) -C $(CURDIR)/port BOARD=X86_64_BIOS all
	$(MAKE) -C $(CURDIR)/port BOARD=X86_64_UEFI all
	$(MAKE) -C $(CURDIR)/port BOARD=ARMV7_RV1106 all
	$(MAKE) -C $(CURDIR)/port BOARD=ARMV7_QEMU all

firmware-prove:
	$(MAKE) -C $(CURDIR)/port BOARD=X86_64_BIOS prove
	$(MAKE) -C $(CURDIR)/port BOARD=X86_64_UEFI prove
	$(MAKE) -C $(CURDIR)/port BOARD=ARMV7_RV1106 prove
	$(MAKE) -C $(CURDIR)/port BOARD=ARMV7_QEMU prove

firmware-check:
	mkdir -p $(CURDIR)/build
	$(CC) -std=gnu11 -Wall -Wextra -Werror -c -o $(CURDIR)/build/firmware_check.o \
		-I$(CURDIR) -I$(TOP) $(CURDIR)/firmware_check.c
	@echo firmware Metal GC/scheduler off ok

upy:
	mkdir -p $(CURDIR)/build
	$(MAKE) -C $(TOP)/ports/unix MICROPY_PY_WASM=1 MICROPY_PY_METAL=1 MICROPY_PY_THREAD_GIL=1 BUILD=build-metal LDFLAGS_EXTRA="-Wl,-no-pie -rdynamic"
	$(TOP)/ports/unix/build-metal/micropython $(CURDIR)/upy_guest_prove.py \
		> $(CURDIR)/build/upy_guest_prove.log 2>&1
	cat $(CURDIR)/build/upy_guest_prove.log
	grep -q "upy jit py object loop" $(CURDIR)/build/upy_guest_prove.log
	grep -q "upy jit cpp rebuild loop" $(CURDIR)/build/upy_guest_prove.log
	grep -q "upy artifact call loop" $(CURDIR)/build/upy_guest_prove.log
	grep -q "upy process budget loop" $(CURDIR)/build/upy_guest_prove.log
	grep -q "upy cross compile wasm loop" $(CURDIR)/build/upy_guest_prove.log
	grep -q "upy cross compile arm loop" $(CURDIR)/build/upy_guest_prove.log
	grep -q "upy types value loop" $(CURDIR)/build/upy_guest_prove.log
	grep -q "upy limits knob loop" $(CURDIR)/build/upy_guest_prove.log
	grep -q "upy room knob loop" $(CURDIR)/build/upy_guest_prove.log
	grep -q "upy knobs under their module" $(CURDIR)/build/upy_guest_prove.log
	grep -q "upy loader image knob" $(CURDIR)/build/upy_guest_prove.log
	grep -q "upy registry row knobs" $(CURDIR)/build/upy_guest_prove.log
	grep -q "upy types row knobs" $(CURDIR)/build/upy_guest_prove.log
	grep -q "upy driver nic knobs" $(CURDIR)/build/upy_guest_prove.log
	grep -q "upy device class knobs" $(CURDIR)/build/upy_guest_prove.log
	grep -q "upy build py unit_compile" $(CURDIR)/build/upy_guest_prove.log
	grep -q "upy build events ring" $(CURDIR)/build/upy_guest_prove.log
	grep -q "upy build all walk" $(CURDIR)/build/upy_guest_prove.log
	$(TOP)/ports/unix/build-metal/micropython $(CURDIR)/upy_serve_prove.py \
		> $(CURDIR)/build/upy_serve.log 2>&1
	grep -q "upy serve fwd mirror" $(CURDIR)/build/upy_serve.log
	grep -q "upy serve inspect faces" $(CURDIR)/build/upy_serve.log
	grep -q "upy serve prove" $(CURDIR)/build/upy_serve.log
	$(TOP)/ports/unix/build-metal/micropython $(CURDIR)/upy_console_prove.py \
		> $(CURDIR)/build/upy_console.log 2>&1 || (cat $(CURDIR)/build/upy_console.log; false)
	grep -q "upy console ring prove" $(CURDIR)/build/upy_console.log
	$(TOP)/ports/unix/build-metal/micropython $(CURDIR)/upy_repl_prove.py \
		> $(CURDIR)/build/upy_repl.log 2>&1 || (cat $(CURDIR)/build/upy_repl.log; false)
	grep -q "upy console panel prove" $(CURDIR)/build/upy_repl.log
	@echo upy console panel mirrors console 0 ok
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
		$(CURDIR)/upy_console_prove.py $(CURDIR)/upy_repl_prove.py \
		> $(CURDIR)/build/browser_prove.log 2>&1
	cat $(CURDIR)/build/browser_prove.log
	grep -q "upy console ring prove" $(CURDIR)/build/browser_prove.log
	grep -q "upy console panel prove" $(CURDIR)/build/browser_prove.log
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
	grep -q "upy build events pane" $(CURDIR)/build/browser_prove.log
	grep -q "upy build refuse keeps ring empty" $(CURDIR)/build/browser_prove.log
	grep -q "upy accessor spine" $(CURDIR)/build/browser_prove.log
	grep -q "upy editor" $(CURDIR)/build/browser_prove.log
	grep -q "upy process" $(CURDIR)/build/browser_prove.log
	grep -q "upy ssh session" $(CURDIR)/build/browser_prove.log
	grep -q "upy zenoh" $(CURDIR)/build/browser_prove.log
	grep -q "upy swarm" $(CURDIR)/build/browser_prove.log
	grep -q "upy jit py object loop" $(CURDIR)/build/browser_prove.log
	grep -q "upy jit cpp rebuild loop" $(CURDIR)/build/browser_prove.log
	grep -q "upy process budget loop" $(CURDIR)/build/browser_prove.log
	grep -q "upy wasm build link" $(CURDIR)/build/browser_prove.log
	grep -q "upy browser cross knob + elf refuse" $(CURDIR)/build/browser_prove.log
	grep -q "upy browser x64+arm cross emit" $(CURDIR)/build/browser_prove.log
	grep -q "upy types value loop" $(CURDIR)/build/browser_prove.log
	grep -q "upy limits knob loop" $(CURDIR)/build/browser_prove.log
	grep -q "upy room knob loop" $(CURDIR)/build/browser_prove.log
	grep -q "upy knobs under their module" $(CURDIR)/build/browser_prove.log
	grep -q "upy loader image knob" $(CURDIR)/build/browser_prove.log
	grep -q "upy registry row knobs" $(CURDIR)/build/browser_prove.log
	grep -q "upy types row knobs" $(CURDIR)/build/browser_prove.log
	grep -q "upy driver nic knobs" $(CURDIR)/build/browser_prove.log
	grep -q "upy device class knobs" $(CURDIR)/build/browser_prove.log
	grep -q "compiled:" $(CURDIR)/build/browser_prove.log
# The console panel's wasm tab boots this same image in the page, out of what
# the seat serves it. Proved here rather than in the cell above: the thing under
# test is the pair of files this lane just produced and the JS that boots them,
# not anything running inside them.
	$(NODE) $(CURDIR)/wasmseat_prove.mjs $(WASM_UPY) \
		> $(CURDIR)/build/wasmseat_prove.log 2>&1
	cat $(CURDIR)/build/wasmseat_prove.log
	grep -q "wasm seat panel prove" $(CURDIR)/build/wasmseat_prove.log

compile-commands:
	python3 $(WASMMOD)/compile_commands.py $(WASMMOD) $(WS) $(VSCODE_CDB)
	python3 $(CURDIR)/compile_commands.py $(CURDIR) $(TOP) $(WASMMOD) $(WS) $(VSCODE_CDB)

clean:
	rm -rf $(CURDIR)/build

# Header dependencies. Without these a card's __types__.h can change the shape
# of a ring and the objects around it are not rebuilt, so the binary is linked
# from two different opinions of that shape. The .d files sit beside the .o
# they describe and only exist for objects that have been built, so whatever is
# there is the whole list — cards, the host's own objects, the wasmmod tests,
# mbedtls, zenoh. (The vendored TCC instances carry their dependency list in
# the manifest instead: tools/tcc.mk's TCC_HDRS.)
-include $(shell find $(CURDIR)/build -name '*.d' 2>/dev/null)
