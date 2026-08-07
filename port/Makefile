# metal port — boards under boards/$(BOARD)/
BOARD ?= X86_64_BIOS
ENGINE ?= mp
REPL ?= 0
BOARD_DIR := boards/$(BOARD)
# Per-ENGINE build dir so LINK_WAMR / TOP CFLAGS never stale-collide.
BUILD ?= build-$(BOARD)-$(ENGINE)

ifeq ($(wildcard $(BOARD_DIR)/build.mk),)
$(error unknown or incomplete BOARD=$(BOARD) — need $(BOARD_DIR)/build.mk)
endif

.PHONY: all run clean
all run clean:
	$(MAKE) -f $(BOARD_DIR)/build.mk $@ BUILD=$(BUILD) BOARD=$(BOARD) ENGINE=$(ENGINE) REPL=$(REPL)
