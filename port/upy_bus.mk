# Minimal wasmmod pm_upy bus for metal into-Py bridges (fn_resolve / fn_call / …).
# Include after WASMMOD / PORT_DIR / BUILD / CFLAGS / OBJ are set.
#
# Browser (MICROPY_PY_WASM=1) already links full wasmmod glue + host.c — skip
# the handle stub and do not double-link glue objects.

# CFLAGS already captured INC earlier on firmware boards — append here.
CFLAGS += -I$(WASMMOD)/include -I$(WASMMOD)
INC += -I$(WASMMOD)/include -I$(WASMMOD)

ifeq ($(MICROPY_PY_WASM),1)
UPY_BUS_LINK_GLUE := 0
else
UPY_BUS_LINK_GLUE := 1
endif

ifeq ($(UPY_BUS_LINK_GLUE),1)
OBJ += \
	$(BUILD)/upy_bus_core.o \
	$(BUILD)/upy_bus_call.o \
	$(BUILD)/upy_bus_module.o \
	$(BUILD)/upy_bus_ops.o \
	$(BUILD)/upy_bus_handles.o

SRC_QSTR += $(PORT_DIR)/upy/wasm_handles_stub.c

$(BUILD)/upy_bus_core.o: $(WASMMOD)/glue/pm_upy/obj/core.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/upy_bus_call.o: $(WASMMOD)/glue/pm_upy/obj/call.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/upy_bus_module.o: $(WASMMOD)/glue/pm_upy/obj/module.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/upy_bus_ops.o: $(WASMMOD)/glue/pm_upy/obj/ops.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/upy_bus_handles.o: $(PORT_DIR)/upy/wasm_handles_stub.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<
endif
