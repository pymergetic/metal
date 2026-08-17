# wasmmod.io + net.cdn on BIOS and UEFI. The Metal side of io.fetch is the
# net.http / net.tls cards, which fw_cards.mk already links from the tree.
# HTTP_NATIVE=0: no POSIX sockets; io.fetch parks in metal.net.http.
CFLAGS_METAL += -DMICROPY_WASM_HTTP_NATIVE=0

FW_OBJS += \
	$(BUILD)/wasmmod_io.o \
	$(BUILD)/wasmmod_cdn.o \
	$(BUILD)/io_ops.o

$(BUILD)/wasmmod_io.o: $(WASMMOD_SRC)/pymergetic/wasmmod/io/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/wasmmod_cdn.o: $(WASMMOD_SRC)/pymergetic/wasmmod/net/cdn/__impl__.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<

$(BUILD)/io_ops.o: $(WASMMOD)/ports/freestanding/io_ops.c | $(BUILD)
	$(CC) $(CFLAGS_METAL) $(INC) -c -o $@ $<
