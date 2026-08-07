# metal port — boards under boards/$(BOARD)/
BOARD ?= X86_64_BIOS
ENGINE ?= mp
REPL ?= 0
LIVE ?= 0
LIVE_SSH ?= 0
BOARD_DIR := boards/$(BOARD)
# Per-ENGINE build dir so LINK_WAMR / TOP CFLAGS never stale-collide.
BUILD ?= build-$(BOARD)-$(ENGINE)$(if $(filter 1,$(LIVE_SSH)),-live-ssh,$(if $(filter 1,$(LIVE)),-live,))

ifeq ($(wildcard $(BOARD_DIR)/build.mk),)
$(error unknown or incomplete BOARD=$(BOARD) — need $(BOARD_DIR)/build.mk)
endif

.PHONY: all run clean live-http live-ssh
all run clean:
	$(MAKE) -f $(BOARD_DIR)/build.mk $@ BUILD=$(BUILD) BOARD=$(BOARD) ENGINE=$(ENGINE) REPL=$(REPL) LIVE=$(LIVE) LIVE_SSH=$(LIVE_SSH)

# Boot with hostfwd, wait for live http, curl :18080, then kill QEMU.
# Force -live BUILD: recipe LIVE=1 must not reuse a smoke BUILD from LIVE=0.
live-http:
	$(MAKE) -f $(BOARD_DIR)/build.mk live-http \
		BUILD=build-$(BOARD)-$(ENGINE)-live \
		BOARD=$(BOARD) ENGINE=$(ENGINE) REPL=0 LIVE=1 LIVE_SSH=0

# Boot with hostfwd, wait for live ssh, read banner on :22022, then kill QEMU.
# Force -live-ssh BUILD: recipe LIVE_SSH=1 must not reuse a smoke BUILD from LIVE_SSH=0.
live-ssh:
	$(MAKE) -f $(BOARD_DIR)/build.mk live-ssh \
		BUILD=build-$(BOARD)-$(ENGINE)-live-ssh \
		BOARD=$(BOARD) ENGINE=$(ENGINE) REPL=0 LIVE=0 LIVE_SSH=1
