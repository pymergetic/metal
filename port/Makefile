# metal port — boards under boards/$(BOARD)/
BOARD ?= X86_64_BIOS
ENGINE ?= mp
REPL ?= 0
LIVE ?= 0
LIVE_SSH ?= 0
BOARD_DIR := boards/$(BOARD)
# Per-ENGINE / mode build dir so CFLAGS never stale-collide.
BUILD ?= build-$(BOARD)-$(ENGINE)$(if $(filter 1,$(REPL)),-repl,$(if $(filter 1,$(LIVE_SSH)),-live-ssh,$(if $(filter 1,$(LIVE)),-live,)))

ifeq ($(wildcard $(BOARD_DIR)/build.mk),)
$(error unknown or incomplete BOARD=$(BOARD) — need $(BOARD_DIR)/build.mk)
endif

.PHONY: all run clean live-http live-ssh repl
all run clean:
	$(MAKE) -f $(BOARD_DIR)/build.mk $@ BUILD=$(BUILD) BOARD=$(BOARD) ENGINE=$(ENGINE) REPL=$(REPL) LIVE=$(LIVE) LIVE_SSH=$(LIVE_SSH)

# Product REPL — lean bring-up + friendly µPy (not smoke).
repl:
	$(MAKE) -f $(BOARD_DIR)/build.mk repl \
		BUILD=build-$(BOARD)-$(ENGINE)-repl \
		BOARD=$(BOARD) ENGINE=$(ENGINE) REPL=1 LIVE=0 LIVE_SSH=0

# Boot with hostfwd, wait for live http, curl :18080, then kill QEMU.
live-http:
	$(MAKE) -f $(BOARD_DIR)/build.mk live-http \
		BUILD=build-$(BOARD)-$(ENGINE)-live \
		BOARD=$(BOARD) ENGINE=$(ENGINE) REPL=0 LIVE=1 LIVE_SSH=0

# Boot with hostfwd, wait for live ssh, read banner on :22022, then kill QEMU.
live-ssh:
	$(MAKE) -f $(BOARD_DIR)/build.mk live-ssh \
		BUILD=build-$(BOARD)-$(ENGINE)-live-ssh \
		BOARD=$(BOARD) ENGINE=$(ENGINE) REPL=0 LIVE=0 LIVE_SSH=1
