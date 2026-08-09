# Silence known -Wsign-compare noise in upstream MicroPython sources only.
# Do NOT apply these to metal / board / port code.
#
# Include before $(TOP)/py/mkrules.mk (target-specific CFLAGS).

$(BUILD)/py/parse.o: CFLAGS += -Wno-sign-compare
$(BUILD)/extmod/modlwip.o: CFLAGS += -Wno-sign-compare
