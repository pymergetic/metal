# Same µPy-vendored mbedtls as host/unix. tls is the real card, not the #else stub.
# wg __crypto__.c needs bignum. Include after CFLAGS_METAL / INC exist.
MBEDTLS_DIR ?= $(abspath $(PORT_DIR)/../../../lib/mbedtls)
CFLAGS_METAL += -DMICROPY_SSL_MBEDTLS=1 \
	-DMBEDTLS_CONFIG_FILE='"mbedtls/mbedtls_config_port.h"' \
	-I$(PORT_DIR) -I$(MBEDTLS_DIR)/include -I$(MBEDTLS_DIR)/library \
	-I$(abspath $(PORT_DIR)/../../..)

# Host list minus aesni/padlock/timing (UEFI -mno-sse; no POSIX timing).
FW_MBEDTLS_SRCS := \
	aes.c asn1parse.c asn1write.c base64.c bignum_core.c bignum_mod.c \
	bignum_mod_raw.c bignum.c ccm.c cipher.c cipher_wrap.c constant_time.c \
	ctr_drbg.c des.c dhm.c ecdh.c ecdsa.c ecp.c ecp_curves.c entropy.c \
	entropy_poll.c gcm.c hmac_drbg.c md5.c md.c oid.c pem.c pk.c pkcs12.c \
	pkcs5.c pkparse.c pk_ecc.c pk_wrap.c pkwrite.c platform.c platform_util.c \
	poly1305.c ripemd160.c rsa.c rsa_alt_helpers.c sha1.c sha256.c sha512.c \
	ssl_cache.c ssl_ciphersuites.c ssl_client.c ssl_cookie.c \
	ssl_debug_helpers_generated.c ssl_msg.c ssl_ticket.c ssl_tls.c \
	ssl_tls12_client.c ssl_tls12_server.c x509.c x509_create.c x509_crl.c \
	x509_crt.c x509_csr.c

FW_OBJS += $(BUILD)/mbedtls_plat.o \
	$(addprefix $(BUILD)/mbedtls/, $(FW_MBEDTLS_SRCS:.c=.o))

$(BUILD)/mbedtls_plat.o: $(PORT_DIR)/mbedtls/mbedtls_plat.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/mbedtls/%.o: $(MBEDTLS_DIR)/library/%.c | $(BUILD)/mbedtls
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/mbedtls:
	mkdir -p $@
