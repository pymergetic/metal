# Generate self-contained MicroPython sources.
# Invoked as:
#   make -f micropython_embed.mk MICROPYTHON_TOP=... BUILD=... PACKAGE_DIR=...
MICROPYTHON_TOP ?= ../../../../../../external/micropython
BUILD ?= ../../../../../../build/micropython-embed-build
PACKAGE_DIR ?= ../../../../../../build/micropython_embed

# extmod/modbinascii.c is not part of the upstream embed package (embed.mk
# only copies extmod/modplatform.h); Metal's own build.d scripts compile it
# directly from external/micropython/extmod/. It still needs to go through
# the same qstr/moduledefs scan as the py/ core sources so that MP_QSTR_binascii
# and its MP_REGISTER_EXTENSIBLE_MODULE entry land in the generated headers.
# Must be set before embed.mk pulls in py/mkrules.mk, whose qstr.i.last rule
# expands $(SRC_QSTR) as a prerequisite list at parse time (a later += is
# too late to affect that rule).
SRC_QSTR += $(MICROPYTHON_TOP)/extmod/modbinascii.c

include $(MICROPYTHON_TOP)/ports/embed/embed.mk
