# Linked after ports/webassembly sets OBJ — not part of SRC_QSTR.
ifeq ($(strip $(METAL_BOOT_SRCS)),)
$(error METAL_BOOT_SRCS empty — mpconfigvariant.mk must define it)
endif

METAL_BOOT_OBJ := $(patsubst $(METAL)/%.c,$(BUILD)/metal/%.o,$(METAL_BOOT_SRCS))
OBJ += $(METAL_BOOT_OBJ)

$(BUILD)/metal/%.o: $(METAL)/%.c
	$(ECHO) "CC $<"
	$(Q)$(MKDIR) -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<
