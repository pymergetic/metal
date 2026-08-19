# Shared source list for the vendored zenoh-pico core, used by every seat that
# links it: host metal.mk and each firmware board (fw_zenoh.mk). One list, one
# glob — a seat that compiles the core has no second copy to drift.
#
# The vendored tree keeps src/system/** out: Metal replaces the whole platform
# layer (plus the unix system dir), exactly like the host build and the Stage-1
# prove. Everything under the transport/protocol/session/... muscle dirs stays.

ifndef PM_METAL_ZENOH_MK
PM_METAL_ZENOH_MK := 1

ifndef ZENOH_PICO_DIR
$(error tools/zenoh.mk included before ZENOH_PICO_DIR was set)
endif

ZP_REL := $(shell find $(ZENOH_PICO_DIR)/src -name '*.c' \
	| grep -E '/(api|collections|link|net|protocol|runtime|session|transport|utils)/' \
	| grep -v '/src/system/' \
	| sed 's#$(ZENOH_PICO_DIR)/##')

endif
