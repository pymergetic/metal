# Stage 1 prove: the vendored zenoh-pico raw lib builds and runs a
# put/subscriber loop on lo, before any Metal card wraps it.
#
# use: make -C extmod/metal prove-zpico     (or: make -f tools/zp_pico_prove/Makefile)
# depends on the pinned submodule: metalpython/lib/zenoh-pico
#
# The lib is compiled against its own unix system layer (z_open/z_spin tools
# only) in single-thread mode (Z_FEATURE_MULTI_THREAD=0) -- the model the
# pymergetic.metal.net.zenoh card reuses for its poll() step.

ZPICO_DIR ?= $(abspath ../../../../lib/zenoh-pico)

CC      ?= cc
AR      ?= ar
CFLAGS  ?= -O1
ZPFLAGS  = -DZENOH_LINUX -I$(ZPICO_DIR)/include $(CFLAGS)
LDLIBS   = -lpthread -ldl -lm

BUILD   := $(abspath .build)

# Object list: one .o per source, named after its relative path under
# ZPICO_DIR, so a single pattern rule can drive the whole lib compile.
#
# Freestanding build = the seat-independent core plus one system layer. The
# unix platform is used here; the Metal card replaces unix/network.c +
# unix/system.c with its own platform shim against pm_metal_net_ip_*. All the
# other board platforms (espidf, rpi_pico, freertos, zephyr, windows, ...) are
# excluded, so no board SDK headers are required.
ZP_REL  := $(shell find $(ZPICO_DIR)/src -name '*.c' \
                | grep -E '/(api|collections|link|net|protocol|runtime|session|transport|utils)/' \
                | sed 's#$(ZPICO_DIR)/##')
ZP_REL  += src/system/common/platform.c src/system/common/serial.c \
           src/system/unix/network.c src/system/unix/system.c
ZP_OBJS := $(addprefix $(BUILD)/,$(sort $(ZP_REL:.c=.o)))

$(BUILD)/%.o: $(ZPICO_DIR)/%.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(ZPFLAGS) -c $< -o $@

$(BUILD)/zenohpico.a: $(ZP_OBJS)
	$(AR) rcs $@ $^

$(BUILD)/prove_pub.o: prove_pub.c | $(BUILD)
	$(CC) $(ZPFLAGS) -c $< -o $@
$(BUILD)/prove_sub.o: prove_sub.c | $(BUILD)
	$(CC) $(ZPFLAGS) -c $< -o $@

prove_pub: $(BUILD)/prove_pub.o $(BUILD)/zenohpico.a
	$(CC) -o $@ $^ $(LDLIBS)

prove_sub: $(BUILD)/prove_sub.o $(BUILD)/zenohpico.a
	$(CC) -o $@ $^ $(LDLIBS)

$(BUILD):
	mkdir -p $(BUILD)

# Start the subscriber (listener) on lo, publish from the connector, assert the
# subscriber received EXPECTED puts, then exit 0 / non-zero.
prove: prove_pub prove_sub
	@rm -f $(BUILD)/sub.log
	@./prove_sub -m peer -l tcp/127.0.0.1:7455 >$(BUILD)/sub.log 2>&1 & \
	  SUBPID=$$!; \
	  sleep 2; \
	  ./prove_pub -m client -e tcp/127.0.0.1:7455; \
	  wait $$SUBPID; \
	  cat $(BUILD)/sub.log; \
	  echo "zenoh-pico lo prove OK"

clean:
	rm -rf $(BUILD) prove_pub prove_sub
