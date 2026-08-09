# µPy glue faces live at $(METAL)/glue (not under port/).
# Include once GLUE/METAL/BUILD/CFLAGS are set, then after OBJ= use:
#   $(eval $(call GLUE_ATTACH))

# Orphan MP_REGISTER_MODULE crumbs from deleted port/common/mod*.c keep
# registering metalnet/ssh/_pm_externals across incremental builds.
ifneq ($(HEADER_BUILD),)
$(shell rm -f $(HEADER_BUILD)/module/common__*.module \
	$(HEADER_BUILD)/qstr/common__*.qstr 2>/dev/null; \
	if grep -qE 'mp_module_metalnet\>|mp_module_ssh\>|mp_module_pm_externals\>' \
		$(HEADER_BUILD)/moduledefs.collected 2>/dev/null; then \
		rm -f $(HEADER_BUILD)/moduledefs.collected $(HEADER_BUILD)/moduledefs.h \
			$(HEADER_BUILD)/moduledefs.split; \
	fi)
endif

GLUE_C = \
	pymergetic/__init__.c \
	pymergetic/metal/__init__.c \
	pymergetic/metal/externals.c \
	pymergetic/metal/auth.c \
	pymergetic/metal/trust.c \
	pymergetic/metal/util/__init__.c \
	pymergetic/metal/util/lz4.c \
	pymergetic/metal/util/size.c \
	pymergetic/metal/util/endian.c \
	pymergetic/metal/util/fourcc.c \
	pymergetic/metal/util/eightcc.c \
	pymergetic/metal/util/tar.c \
	pymergetic/metal/net/__init__.c \
	pymergetic/metal/net/ip.c \
	pymergetic/metal/net/wg.c \
	pymergetic/metal/net/ssh.c

define GLUE_ATTACH
SRC_QSTR += $(addprefix $(GLUE)/, $(GLUE_C))
OBJ += $(addprefix $(BUILD)/glue/, $(GLUE_C:.c=.o))
endef

$(BUILD)/glue/%.o: $(GLUE)/%.c
	$(ECHO) "CC $<"
	$(Q)$(MKDIR) -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<
