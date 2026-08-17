# metal firmware port — QEMU BIOS/UEFI seats for the new pymergetic.metal cards.
BOARD ?= X86_64_BIOS
ENGINE ?= mp
REPL ?= 1
BOARD_DIR := boards/$(BOARD)
BUILD ?= build/$(BOARD)-$(ENGINE)$(if $(filter 1,$(REPL)),-repl,)

ifeq ($(wildcard $(BOARD_DIR)/build.mk),)
$(error unknown BOARD=$(BOARD) — need $(BOARD_DIR)/build.mk)
endif

.PHONY: all run prove upload clean menu
all run upload clean:
	$(MAKE) -f $(BOARD_DIR)/build.mk $@ BUILD=$(BUILD) BOARD=$(BOARD) ENGINE=$(ENGINE) REPL=$(REPL)

# One-shot autoexec then halt. Interactive UART is `run` (REPL=1).
prove:
	$(MAKE) -f $(BOARD_DIR)/build.mk prove BUILD=build/$(BOARD)-$(ENGINE) BOARD=$(BOARD) ENGINE=$(ENGINE) REPL=0

menu:
	$(MAKE) -C $(abspath $(CURDIR)/..) menu
