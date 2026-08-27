# Shared source list for the vendored zenoh-pico core, used by every seat that
# links it: host metal.mk and each firmware board (fw_zenoh.mk). One list, one
# definition — the externals/zenoh-pico/__pmm__.toml manifest. The list itself
# is regenerable via `tools/externals.sh gen zenoh-pico`.
#
# The vendored tree keeps src/system/** out: Metal replaces the whole platform
# layer (plus the unix system dir), exactly like the host build and the Stage-1
# prove. Everything under the transport/protocol/session/... muscle dirs stays.

ifndef PM_METAL_ZENOH_MK
PM_METAL_ZENOH_MK := 1

ifndef ZENOH_PICO_DIR
$(error tools/zenoh.mk included before ZENOH_PICO_DIR was set)
endif

EXTERNALS_SH := $(dir $(lastword $(MAKEFILE_LIST)))externals.sh
ZP_REL := $(shell $(EXTERNALS_SH) list zenoh-pico)

ifeq ($(ZP_REL),)
$(error tools/zenoh.mk: externals.sh list zenoh-pico returned nothing)
endif

endif
