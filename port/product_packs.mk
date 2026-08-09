# Build-time MPWP embeds for metal product modules only.
# Output: $(BUILD)/packs/ — never under src/.
# pymergetic.wasmmod is NOT generated here (wasmmod embed-host owns that).

PACK_OUT := $(BUILD)/packs
PACK_GEN := $(METAL)/scripts/gen_product_packs.sh
PACK_SRCS := \
	$(METAL)/httpd.json \
	$(METAL)/scripts/gen_mpwp_pack.py \
	$(METAL)/scripts/gen_product_packs.sh \
	$(wildcard $(METAL)/src/pymergetic/metal/inspect/*.py) \
	$(wildcard $(METAL)/src/pymergetic/metal/inspect/www/inspect/*) \
	$(wildcard $(METAL)/src/pymergetic/metal/inspect/www/inspect/*/*) \
	$(wildcard $(METAL)/src/pymergetic/metal/inspect/www/inspect/*/*/*)

$(PACK_OUT)/pack_inspect_embed.c $(PACK_OUT)/pack_metal_embed.c: $(PACK_SRCS) | $(BUILD)
	$(ECHO) "GEN product packs → $(PACK_OUT)"
	$(Q)$(PACK_GEN) $(PACK_OUT)

$(BUILD)/metal_pack_inspect.o: $(PACK_OUT)/pack_inspect_embed.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_pack_metal.o: $(PACK_OUT)/pack_metal_embed.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<
