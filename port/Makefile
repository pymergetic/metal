# metal firmware port — QEMU BIOS/UEFI seats for the new pymergetic.metal cards.
BOARD ?= X86_64_BIOS
ENGINE ?= mp
REPL ?= 1
BOARD_DIR := boards/$(BOARD)
BUILD ?= build/$(BOARD)-$(ENGINE)$(if $(filter 1,$(REPL)),-repl,)

ifeq ($(wildcard $(BOARD_DIR)/build.mk),)
$(error unknown BOARD=$(BOARD) — need $(BOARD_DIR)/build.mk)
endif

.PHONY: all run clean
all run clean:
	$(MAKE) -f $(BOARD_DIR)/build.mk $@ BUILD=$(BUILD) BOARD=$(BOARD) ENGINE=$(ENGINE) REPL=$(REPL)
