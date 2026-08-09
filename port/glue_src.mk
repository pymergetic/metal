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
	pymergetic/metal/register_modules.c \
	pymergetic/metal/externals.c \
	pymergetic/metal/auth.c \
	pymergetic/metal/trust.c \
	pymergetic/metal/async.c \
	pymergetic/metal/console.c \
	pymergetic/metal/draw.c \
	pymergetic/metal/pack.c \
	pymergetic/metal/util/__init__.c \
	pymergetic/metal/util/lz4.c \
	pymergetic/metal/util/size.c \
	pymergetic/metal/util/endian.c \
	pymergetic/metal/util/fourcc.c \
	pymergetic/metal/util/eightcc.c \
	pymergetic/metal/util/ascii.c \
	pymergetic/metal/util/tar.c \
	pymergetic/metal/boot/__init__.c \
	pymergetic/metal/boot/tree.c \
	pymergetic/metal/bus/__init__.c \
	pymergetic/metal/bus/pci.c \
	pymergetic/metal/bus/virtio.c \
	pymergetic/metal/mem/__init__.c \
	pymergetic/metal/mem/port.c \
	pymergetic/metal/net/__init__.c \
	pymergetic/metal/net/ip.c \
	pymergetic/metal/net/wg.c \
	pymergetic/metal/net/ssh.c \
	pymergetic/metal/net/faces.c \
	pymergetic/metal/net/asgi.c \
	pymergetic/metal/net/dhcp.c \
	pymergetic/metal/net/dns.c \
	pymergetic/metal/net/http.c \
	pymergetic/metal/net/nic.c \
	pymergetic/metal/net/ntp.c \
	pymergetic/metal/net/pump.c \
	pymergetic/metal/net/tftp.c \
	pymergetic/metal/net/tls.c \
	pymergetic/metal/dev/__init__.c \
	pymergetic/metal/dev/serial.c \
	pymergetic/metal/dev/acpi.c \
	pymergetic/metal/dev/blk.c \
	pymergetic/metal/dev/stream.c \
	pymergetic/metal/dev/gfx/__init__.c \
	pymergetic/metal/dev/gfx/compositor.c \
	pymergetic/metal/dev/gfx/scanout.c \
	pymergetic/metal/dev/gfx/text.c \
	pymergetic/metal/dev/input/__init__.c \
	pymergetic/metal/dev/input/kbd.c \
	pymergetic/metal/dev/net/__init__.c \
	pymergetic/metal/dev/net/bge.c \
	pymergetic/metal/dev/net/virtio_net.c \
	pymergetic/metal/shell/__init__.c \
	pymergetic/metal/shell/tui.c \
	pymergetic/metal/shell/ui.c \
	pymergetic/metal/shell/vt.c \
	pymergetic/metal/fs/__init__.c \
	pymergetic/metal/fs/embed.c \
	pymergetic/metal/fs/fat.c \
	pymergetic/metal/fs/littlefs.c \
	pymergetic/metal/fs/mtar.c \
	pymergetic/metal/fs/overlay.c \
	pymergetic/metal/fs/tmpfs.c \
	pymergetic/metal/fs/vfs.c \
	pymergetic/metal/fs/wasmmod.c \
	pymergetic/metal/fs/zip.c \
	pymergetic/metal/mem/arena.c \
	pymergetic/metal/mem/lock.c \
	pymergetic/metal/mem/tlsf.c \
	pymergetic/metal/rt.c \
	pymergetic/metal/hwtree.c \
	pymergetic/metal/wamr_host.c

define GLUE_ATTACH
SRC_QSTR += $(addprefix $(GLUE)/, $(GLUE_C))
OBJ += $(addprefix $(BUILD)/glue/, $(GLUE_C:.c=.o))
endef

$(BUILD)/glue/%.o: $(GLUE)/%.c
	$(ECHO) "CC $<"
	$(Q)$(MKDIR) -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<
