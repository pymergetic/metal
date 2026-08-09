# metal port — boards under boards/$(BOARD)/
BOARD ?= X86_64_BIOS
ENGINE ?= mp
REPL ?= 0
LIVE ?= 0
LIVE_SSH ?= 0
SMP ?= 2
BOARD_DIR := boards/$(BOARD)
# Per-ENGINE / mode build dir so CFLAGS never stale-collide.
BUILD ?= build/$(BOARD)-$(ENGINE)$(if $(filter 1,$(REPL)),-repl,$(if $(filter 1,$(LIVE_SSH)),-live-ssh,$(if $(filter 1,$(LIVE)),-live,)))

ifeq ($(wildcard $(BOARD_DIR)/build.mk),)
$(error unknown or incomplete BOARD=$(BOARD) — need $(BOARD_DIR)/build.mk)
endif

.PHONY: all run clean live-http live-ssh repl repl-check vnc
all run clean:
	$(MAKE) -f $(BOARD_DIR)/build.mk $@ BUILD=$(BUILD) BOARD=$(BOARD) ENGINE=$(ENGINE) REPL=$(REPL) LIVE=$(LIVE) LIVE_SSH=$(LIVE_SSH) SMP=$(SMP)

# Interactive product REPL on serial stdio (type at >>>). Scripted prove: repl-check.
# Default VNC=0 — leftover QEMU often holds :5900; pass VNC=1 if you want TightVNC too.
repl repl-check:
	$(MAKE) -f $(BOARD_DIR)/build.mk $@ \
		BUILD=build/$(BOARD)-$(ENGINE)-repl \
		BOARD=$(BOARD) ENGINE=$(ENGINE) REPL=1 LIVE=0 LIVE_SSH=0 SMP=$(SMP) VNC=$(or $(VNC),0)

# Long-lived guest + VNC (stays up until Ctrl-C).
vnc:
	$(MAKE) -f $(BOARD_DIR)/build.mk vnc \
		BUILD=build/$(BOARD)-$(ENGINE)-live \
		BOARD=$(BOARD) ENGINE=$(ENGINE) REPL=0 LIVE=1 LIVE_SSH=0 SMP=$(SMP)

# Boot with hostfwd, wait for live http, curl :18080, then kill QEMU.
live-http:
	$(MAKE) -f $(BOARD_DIR)/build.mk live-http \
		BUILD=build/$(BOARD)-$(ENGINE)-live \
		BOARD=$(BOARD) ENGINE=$(ENGINE) REPL=0 LIVE=1 LIVE_SSH=0 SMP=$(SMP)

# Boot with hostfwd, wait for live ssh, read banner on :22022, then kill QEMU.
live-ssh:
	$(MAKE) -f $(BOARD_DIR)/build.mk live-ssh \
		BUILD=build/$(BOARD)-$(ENGINE)-live-ssh \
		BOARD=$(BOARD) ENGINE=$(ENGINE) REPL=0 LIVE=0 LIVE_SSH=1 SMP=$(SMP)
