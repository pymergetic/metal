# Metal port — link µPy lib/lwip + Metal lwIP glue (NO_SYS).
# Include from board build.mk after TOP/METAL/BUILD/CFLAGS are set.

LWIP_DIR := $(TOP)/lib/lwip/src
LWIP_INC := $(PORT_DIR)/lwip_inc
WG_DIR := $(METAL)/third_party/wireguard-lwip/src

CFLAGS += -I$(LWIP_INC) -I$(LWIP_DIR)/include -I$(WG_DIR) -DMICROPY_PY_LWIP=1

# Stale modsocket.* from pre-lwIP builds poisons moduledefs (mp_module_socket).
$(shell rm -f $(BUILD)/extmod/modsocket.o $(BUILD)/extmod/modsocket.P \
	$(BUILD)/genhdr/module/*modsocket* 2>/dev/null)

# objmodule.o embeds the module table; force rebuild when moduledefs.h changes
# (depfiles sometimes miss this after SRC_C drops modsocket).
$(BUILD)/py/objmodule.o: $(BUILD)/genhdr/moduledefs.h

LWIP_SRC_C := \
	$(LWIP_DIR)/core/init.c \
	$(LWIP_DIR)/core/def.c \
	$(LWIP_DIR)/core/dns.c \
	$(LWIP_DIR)/core/inet_chksum.c \
	$(LWIP_DIR)/core/ip.c \
	$(LWIP_DIR)/core/mem.c \
	$(LWIP_DIR)/core/memp.c \
	$(LWIP_DIR)/core/netif.c \
	$(LWIP_DIR)/core/pbuf.c \
	$(LWIP_DIR)/core/raw.c \
	$(LWIP_DIR)/core/stats.c \
	$(LWIP_DIR)/core/sys.c \
	$(LWIP_DIR)/core/tcp.c \
	$(LWIP_DIR)/core/tcp_in.c \
	$(LWIP_DIR)/core/tcp_out.c \
	$(LWIP_DIR)/core/timeouts.c \
	$(LWIP_DIR)/core/udp.c \
	$(LWIP_DIR)/core/ipv4/dhcp.c \
	$(LWIP_DIR)/core/ipv4/etharp.c \
	$(LWIP_DIR)/core/ipv4/icmp.c \
	$(LWIP_DIR)/core/ipv4/igmp.c \
	$(LWIP_DIR)/core/ipv4/ip4.c \
	$(LWIP_DIR)/core/ipv4/ip4_addr.c \
	$(LWIP_DIR)/core/ipv4/ip4_frag.c \
	$(LWIP_DIR)/netif/ethernet.c

WG_SRC_C := \
	$(WG_DIR)/wireguard.c \
	$(WG_DIR)/wireguardif.c \
	$(WG_DIR)/crypto.c \
	$(WG_DIR)/crypto/refc/blake2s.c \
	$(WG_DIR)/crypto/refc/chacha20.c \
	$(WG_DIR)/crypto/refc/chacha20poly1305.c \
	$(WG_DIR)/crypto/refc/poly1305-donna.c \
	$(WG_DIR)/crypto/refc/x25519.c

METAL_LWIP_OBJ := \
	$(BUILD)/metal_lwip_sys.o \
	$(BUILD)/metal_ip_lwip_netif.o \
	$(BUILD)/metal_ip_lwip_sock.o \
	$(BUILD)/metal_wg.o \
	$(BUILD)/metal_wg_platform.o \
	$(BUILD)/mpnetworkport.o

LWIP_OBJ := $(addprefix $(BUILD)/lwip/, $(notdir $(LWIP_SRC_C:.c=.o)))
WG_OBJ := $(addprefix $(BUILD)/wg/, $(notdir $(WG_SRC_C:.c=.o)))

OBJ += $(METAL_LWIP_OBJ) $(LWIP_OBJ) $(WG_OBJ)

$(BUILD)/lwip:
	$(Q)$(MKDIR) -p $@

$(BUILD)/wg:
	$(Q)$(MKDIR) -p $@

define LWIP_COMPILE_RULE
$(BUILD)/lwip/$(notdir $(1:.c=.o)): $(1) | $(BUILD)/lwip
	$$(ECHO) "CC $$<"
	$$(Q)$$(CC) $$(CFLAGS) -Wno-address -c -o $$@ $$<
endef
$(foreach f,$(LWIP_SRC_C),$(eval $(call LWIP_COMPILE_RULE,$(f))))

define WG_COMPILE_RULE
$(BUILD)/wg/$(notdir $(1:.c=.o)): $(1) | $(BUILD)/wg
	$$(ECHO) "CC $$<"
	$$(Q)$$(CC) $$(CFLAGS) -Werror -c -o $$@ $$<
endef
$(foreach f,$(WG_SRC_C),$(eval $(call WG_COMPILE_RULE,$(f))))

$(BUILD)/metal_lwip_sys.o: $(METAL)/src/pymergetic/metal/net/ip/lwip_sys.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_ip_lwip_netif.o: $(METAL)/src/pymergetic/metal/net/ip/ip_lwip_netif.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -I$(METAL)/src/pymergetic/metal/net/ip -c -o $@ $<

$(BUILD)/metal_ip_lwip_sock.o: $(METAL)/src/pymergetic/metal/net/ip/ip_lwip_sock.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -I$(METAL)/src/pymergetic/metal/net/ip -c -o $@ $<

$(BUILD)/metal_wg.o: $(METAL)/src/pymergetic/metal/net/wg/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_wg_platform.o: $(METAL)/src/pymergetic/metal/net/wg/wireguard_platform_metal.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/mpnetworkport.o: $(UPY)/mpnetworkport.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<
