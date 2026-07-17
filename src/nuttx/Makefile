# SPDX-License-Identifier: Apache-2.0
#
# Classic Application.mk path is intentionally thin: WAMR's nuttx wamr.mk is
# driven by CONFIG_INTERPRETERS_WAMR_* (stock WAMR interpreter app). Metal sets
# WAMR_BUILD_* itself in CMakeLists.txt instead — use the NuttX CMake build
# (scripts/build-nuttx-sim.sh). Make.defs still registers this app directory.

include $(APPDIR)/Make.defs

ifeq ($(CONFIG_PYMERGETIC_METAL),y)
$(warning pm_metal: build via NuttX CMake (src/nuttx/CMakeLists.txt), not Application.mk)
endif

# Empty module so a mistaken Make-only apps build fails loudly rather than
# linking a stub with no sources.
PROGNAME  = $(CONFIG_PYMERGETIC_METAL_PROGNAME)
PRIORITY  = $(CONFIG_PYMERGETIC_METAL_PRIORITY)
STACKSIZE = $(CONFIG_PYMERGETIC_METAL_STACKSIZE)
MODULE    = $(CONFIG_PYMERGETIC_METAL)
MAINSRC   =

include $(APPDIR)/Application.mk
