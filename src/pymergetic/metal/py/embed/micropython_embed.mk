# Generate self-contained MicroPython sources.
# Invoked as:
#   make -f micropython_embed.mk MICROPYTHON_TOP=... BUILD=... PACKAGE_DIR=...
MICROPYTHON_TOP ?= ../../../../../../external/micropython
BUILD ?= ../../../../../../build/micropython-embed-build
PACKAGE_DIR ?= ../../../../../../build/micropython_embed

include $(MICROPYTHON_TOP)/ports/embed/embed.mk
