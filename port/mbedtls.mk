# Metal port — link lib/mbedtls for net/tls (NO µPy modtls).
# Include from board build.mk after TOP/METAL/BUILD/PORT_DIR/CFLAGS are set.

MBEDTLS_DIR := $(TOP)/lib/mbedtls
MBEDTLS_LIB := $(MBEDTLS_DIR)/library
MBEDTLS_CFG_INC := $(PORT_DIR)

CFLAGS += -DMICROPY_SSL_MBEDTLS=1 \
	-DMBEDTLS_CONFIG_FILE=\"mbedtls/mbedtls_config_port.h\" \
	-I$(MBEDTLS_CFG_INC) \
	-I$(MBEDTLS_DIR)/include \
	-I$(TOP)

MBEDTLS_SRC_C := \
	$(MBEDTLS_LIB)/aes.c \
	$(MBEDTLS_LIB)/asn1parse.c \
	$(MBEDTLS_LIB)/asn1write.c \
	$(MBEDTLS_LIB)/base64.c \
	$(MBEDTLS_LIB)/bignum_core.c \
	$(MBEDTLS_LIB)/bignum_mod.c \
	$(MBEDTLS_LIB)/bignum_mod_raw.c \
	$(MBEDTLS_LIB)/bignum.c \
	$(MBEDTLS_LIB)/ccm.c \
	$(MBEDTLS_LIB)/chacha20.c \
	$(MBEDTLS_LIB)/chachapoly.c \
	$(MBEDTLS_LIB)/cipher.c \
	$(MBEDTLS_LIB)/cipher_wrap.c \
	$(MBEDTLS_LIB)/constant_time.c \
	$(MBEDTLS_LIB)/ctr_drbg.c \
	$(MBEDTLS_LIB)/debug.c \
	$(MBEDTLS_LIB)/ecdh.c \
	$(MBEDTLS_LIB)/ecdsa.c \
	$(MBEDTLS_LIB)/ecp.c \
	$(MBEDTLS_LIB)/ecp_curves.c \
	$(MBEDTLS_LIB)/entropy.c \
	$(MBEDTLS_LIB)/entropy_poll.c \
	$(MBEDTLS_LIB)/gcm.c \
	$(MBEDTLS_LIB)/hmac_drbg.c \
	$(MBEDTLS_LIB)/md5.c \
	$(MBEDTLS_LIB)/md.c \
	$(MBEDTLS_LIB)/oid.c \
	$(MBEDTLS_LIB)/pem.c \
	$(MBEDTLS_LIB)/pk.c \
	$(MBEDTLS_LIB)/pkcs5.c \
	$(MBEDTLS_LIB)/pkparse.c \
	$(MBEDTLS_LIB)/pk_ecc.c \
	$(MBEDTLS_LIB)/pk_wrap.c \
	$(MBEDTLS_LIB)/platform.c \
	$(MBEDTLS_LIB)/platform_util.c \
	$(MBEDTLS_LIB)/poly1305.c \
	$(MBEDTLS_LIB)/rsa.c \
	$(MBEDTLS_LIB)/rsa_alt_helpers.c \
	$(MBEDTLS_LIB)/sha1.c \
	$(MBEDTLS_LIB)/sha256.c \
	$(MBEDTLS_LIB)/sha512.c \
	$(MBEDTLS_LIB)/ssl_cache.c \
	$(MBEDTLS_LIB)/ssl_ciphersuites.c \
	$(MBEDTLS_LIB)/ssl_client.c \
	$(MBEDTLS_LIB)/ssl_debug_helpers_generated.c \
	$(MBEDTLS_LIB)/ssl_msg.c \
	$(MBEDTLS_LIB)/ssl_tls.c \
	$(MBEDTLS_LIB)/ssl_tls12_client.c \
	$(MBEDTLS_LIB)/ssl_tls12_server.c \
	$(MBEDTLS_LIB)/timing.c \
	$(MBEDTLS_LIB)/x509.c \
	$(MBEDTLS_LIB)/x509_crl.c \
	$(MBEDTLS_LIB)/x509_crt.c \
	$(MBEDTLS_LIB)/error.c

MBEDTLS_OBJ := $(addprefix $(BUILD)/mbedtls/, $(notdir $(MBEDTLS_SRC_C:.c=.o)))
METAL_MBEDTLS_PORT_OBJ := $(BUILD)/metal_mbedtls_port.o
METAL_TLS_OBJ := $(BUILD)/metal_tls.o $(BUILD)/metal_tls_mbedtls.o $(BUILD)/metal_tls_smoke.o \
	$(BUILD)/metal_http_client.o $(BUILD)/metal_asgi_ws.o

OBJ += $(METAL_MBEDTLS_PORT_OBJ) $(METAL_TLS_OBJ) $(MBEDTLS_OBJ)

# UEFI uses --target=*-windows (defines _WIN32/_MSC_VER).
# -U_MSC_VER: skip sal.h / winsock2 MSVC branches.
# -DEFIX64: mbedtls treats UEFI as non-Win32 for time/FS (see x509_crt.c).
# platform_util.c: -U_WIN32 so ZEROIZE_ALT path does not include windows.h.
# x509_crt.c: keep _WIN32 + -U_MSC_VER → software inet_pton (no host sockets).
MBEDTLS_CFLAGS := $(CFLAGS) -Wno-unused-parameter -U_MSC_VER -DEFIX64
MBEDTLS_CFG := $(PORT_DIR)/mbedtls/mbedtls_config_port.h

$(BUILD)/mbedtls:
	$(Q)$(MKDIR) -p $@

# Exclude objs with custom recipes from the generic foreach rule.
MBEDTLS_SRC_C_GENERIC := $(filter-out \
	$(MBEDTLS_LIB)/platform_util.c \
	$(MBEDTLS_LIB)/x509_crt.c \
	,$(MBEDTLS_SRC_C))

define MBEDTLS_COMPILE_RULE
$(BUILD)/mbedtls/$(notdir $(1:.c=.o)): $(1) $(MBEDTLS_CFG) | $(BUILD)/mbedtls
	$$(ECHO) "CC $$<"
	$$(Q)$$(CC) $$(MBEDTLS_CFLAGS) -c -o $$@ $$<
endef
$(foreach f,$(MBEDTLS_SRC_C_GENERIC),$(eval $(call MBEDTLS_COMPILE_RULE,$(f))))

$(BUILD)/mbedtls/platform_util.o: $(MBEDTLS_LIB)/platform_util.c $(MBEDTLS_CFG) | $(BUILD)/mbedtls
	$(ECHO) "CC $<"
	$(Q)$(CC) $(MBEDTLS_CFLAGS) -U_WIN32 -UWIN32 -U_WIN64 -c -o $@ $<

$(BUILD)/mbedtls/x509_crt.o: $(MBEDTLS_LIB)/x509_crt.c $(MBEDTLS_CFG) | $(BUILD)/mbedtls
	$(ECHO) "CC $<"
	$(Q)$(CC) $(MBEDTLS_CFLAGS) -D_WIN32 -c -o $@ $<

$(BUILD)/metal_mbedtls_port.o: $(PORT_DIR)/mbedtls/mbedtls_port.c $(MBEDTLS_CFG) | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(MBEDTLS_CFLAGS) -c -o $@ $<

$(BUILD)/metal_tls.o: $(METAL)/src/pymergetic/metal/net/tls/__init__.c $(MBEDTLS_CFG) | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(MBEDTLS_CFLAGS) -c -o $@ $<

$(BUILD)/metal_tls_mbedtls.o: $(METAL)/src/pymergetic/metal/net/tls/mbedtls_backend.c $(MBEDTLS_CFG) | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(MBEDTLS_CFLAGS) -c -o $@ $<

$(BUILD)/metal_tls_smoke.o: $(METAL)/src/pymergetic/metal/net/tls/smoke_certs.c $(MBEDTLS_CFG) | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(MBEDTLS_CFLAGS) -I$(METAL)/src/pymergetic/metal/net/tls -c -o $@ $<

$(BUILD)/metal_http_client.o: $(METAL)/src/pymergetic/metal/net/http/client.c $(MBEDTLS_CFG) | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(MBEDTLS_CFLAGS) -c -o $@ $<

$(BUILD)/metal_asgi_ws.o: $(METAL)/src/pymergetic/metal/net/asgi/ws.c $(MBEDTLS_CFG) | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(MBEDTLS_CFLAGS) -I$(METAL)/src/pymergetic/metal/net/asgi -c -o $@ $<
