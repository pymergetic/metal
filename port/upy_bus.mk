# Minimal wasmmod pm_upy bus for metal into-Py bridges (fn_resolve / fn_call / …).
# Used by inspect / arch(+seats) / unix seats / microdot. Include after
# WASMMOD / PORT_DIR / BUILD / CFLAGS / OBJ are set.
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

# mp is the only product line that links TLSF (pymergetic.metal.mem) — route
# pm_mod's border allocator through it on every mp platform (BIOS/EFI here,
# and mp's own unix port, wherever that links this same mod.c). mpwm/upy/plain
# unix never link mem.c/tlsf.c, so they must keep alloc.h's libc default —
# do not touch MICROPY_WASM_MALLOC for them. Scoped to mod.c's own compile
# recipe (not global CFLAGS) — pymergetic/metal/mem.h's uint8_t*-typed
# declarations conflict with mem/port/__init__.h's void*-typed ones, and
# forcing mem.h into every metal TU would trip that pre-existing mismatch.
ifeq ($(ENGINE),mp)
PM_MOD_WASM_MALLOC_CFLAGS := -DMICROPY_WASM_MALLOC=pm_metal_mem_alloc -DMICROPY_WASM_FREE=pm_metal_mem_free \
	-DMICROPY_WASM_REALLOC=pm_metal_mem_realloc -include pymergetic/metal/mem.h
endif

ifeq ($(UPY_BUS_LINK_GLUE),1)
OBJ += \
	$(BUILD)/upy_bus_core.o \
	$(BUILD)/upy_bus_call.o \
	$(BUILD)/upy_bus_module.o \
	$(BUILD)/upy_bus_ops.o \
	$(BUILD)/upy_bus_attr.o \
	$(BUILD)/upy_bus_handles.o \
	$(BUILD)/upy_bus_pm_mod.o

SRC_QSTR += $(PORT_DIR)/upy/wasm_handles_stub.c
SRC_QSTR += $(WASMMOD)/mod.c

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

$(BUILD)/upy_bus_attr.o: $(WASMMOD)/glue/pm_upy/obj/attr.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/upy_bus_handles.o: $(PORT_DIR)/upy/wasm_handles_stub.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/upy_bus_pm_mod.o: $(WASMMOD)/mod.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) $(PM_MOD_WASM_MALLOC_CFLAGS) -c -o $@ $<
endif
