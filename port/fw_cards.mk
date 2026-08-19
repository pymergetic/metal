# Every C card, on every board. The list is the card tree (tools/cards.sh),
# so a new card links on BIOS, UEFI and RV1106 the moment it has a manifest —
# nobody has to remember to add an object here. Fills may differ (tap has no
# /dev/net/tun on firmware); the card is still linked. RS ASGI is fw_lock.
#
# Objects mirror the card path under $(BUILD)/cards, so drivers/net/sim and
# drivers/rtc/sim cannot collide the way flat object names did.
#
# Include after FW_OBJS is started, with $(METAL_DIR) and $(METAL_SRC) set.

WASMMOD_GEN_ROOTS := $(METAL_SRC)
include $(WASMMOD)/gen.mk
include $(METAL_DIR)/tools/www.mk
include $(METAL_DIR)/tools/src.mk

FW_CARD_REL := $(shell $(METAL_DIR)/tools/cards.sh impl $(METAL_SRC)/pymergetic/metal)
ifeq ($(FW_CARD_REL),)
$(error metal card discovery failed — see tools/cards.sh output above)
endif

FW_CARD_OBJS := $(addprefix $(BUILD)/cards/,$(FW_CARD_REL:.c=.o))
FW_OBJS += $(FW_CARD_OBJS)

$(FW_CARD_OBJS): $(BUILD)/cards/%.o: $(METAL_SRC)/pymergetic/metal/%.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<
