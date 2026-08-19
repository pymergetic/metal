# Same vendored zenoh-pico core as host/unix, on every firmware seat. The net.zenoh
# card's platform shim (platform_metal.c / platform_metal_sys.c) plus the card border
# (__impl__.c) build via fw_cards.mk; this file adds the vendored freestanding core
# objects and the flags that make BOTH the core and the card reach zenoh-pico's
# GENERIC config/platform headers in ZENOH_CARD_DIR.
#
# zenoh-pico's api/macros.h is built on C11 _Generic, so the zenoh objects (core and
# card) must compile with -std=gnu11 — the board default is gnu99. The longer-prefix
# pattern rule for cards/net/zenoh/% overrides fw_cards.mk's generic cards/% rule
# (shortest-stem match wins), and the core rule omits -Werror, matching how the host
# metal.mk and mbedtls treat external trees.
ZENOH_PICO_DIR ?= $(abspath $(PORT_DIR)/../../../lib/zenoh-pico)
ZENOH_CARD_DIR ?= $(METAL_SRC)/pymergetic/metal/net/zenoh
include $(METAL_DIR)/tools/zenoh.mk

CFLAGS_METAL += -DZENOH_GENERIC \
	-I$(ZENOH_PICO_DIR)/include -I$(ZENOH_PICO_DIR)/src -I$(ZENOH_CARD_DIR)

FW_OBJS += $(addprefix $(BUILD)/zenoh-pico/,$(ZP_REL:.c=.o))

$(BUILD)/zenoh-pico/%.o: $(ZENOH_PICO_DIR)/%.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_METAL) $(INC) -std=gnu11 -Wall -Wextra -c -o $@ $<

# zenoh card border + platform shim need C11 for the _Generic face macros. The
# board's fw_cards.mk rule compiles cards/% with the gnu99 default; the more
# specific net/zenoh/% rule (shorter stem) wins for these objects only.
$(BUILD)/cards/net/zenoh/%.o: $(METAL_SRC)/pymergetic/metal/net/zenoh/%.c | $(BUILD)/cards/net/zenoh
	$(CC) $(CFLAGS_METAL) $(INC) -std=gnu11 -c -o $@ $<

$(BUILD)/cards/net/zenoh:
	@mkdir -p $@
