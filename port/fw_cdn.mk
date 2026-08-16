# wasmmod.io + net.cdn + Metal HTTP fill — same cards on BIOS and UEFI.
# HTTP_NATIVE=0: no POSIX sockets; io.fetch parks in metal.net.http.
CFLAGS_METAL += -DMICROPY_WASM_HTTP_NATIVE=0

FW_OBJS += \
	$(BUILD)/wasmmod_io.o \
	$(BUILD)/wasmmod_cdn.o \
	$(BUILD)/io_ops.o \
	$(BUILD)/tls.o \
	$(BUILD)/http.o

$(BUILD)/wasmmod_io.o: $(WASMMOD_SRC)/pymergetic/wasmmod/io/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/wasmmod_cdn.o: $(WASMMOD_SRC)/pymergetic/wasmmod/net/cdn/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/io_ops.o: $(WASMMOD)/ports/metal/io_ops.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/tls.o: $(METAL_SRC)/pymergetic/metal/net/tls/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/http.o: $(METAL_SRC)/pymergetic/metal/net/http/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<
