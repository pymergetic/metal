# metal port — boards under boards/$(BOARD)/
BOARD ?= X86_64_BIOS
ENGINE ?= mp
REPL ?= 0
LIVE ?= 0
BOARD_DIR := boards/$(BOARD)
# Per-ENGINE build dir so LINK_WAMR / TOP CFLAGS never stale-collide.
BUILD ?= build-$(BOARD)-$(ENGINE)$(if $(filter 1,$(LIVE)),-live,)

ifeq ($(wildcard $(BOARD_DIR)/build.mk),)
$(error unknown or incomplete BOARD=$(BOARD) — need $(BOARD_DIR)/build.mk)
endif

.PHONY: all run clean live-http
all run clean:
	$(MAKE) -f $(BOARD_DIR)/build.mk $@ BUILD=$(BUILD) BOARD=$(BOARD) ENGINE=$(ENGINE) REPL=$(REPL) LIVE=$(LIVE)

# Boot with hostfwd, wait for live http, curl :18080, then kill QEMU.
live-http:
	$(MAKE) -f $(BOARD_DIR)/build.mk live-http BUILD=$(BUILD) BOARD=$(BOARD) ENGINE=$(ENGINE) REPL=0 LIVE=1
