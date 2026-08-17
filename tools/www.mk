# inspect/www_embed.inc.h — bytes from www/, not a second copy of the HTML.
#
# Include from every seat that compiles inspect (host Makefile, metal.mk,
# fw_cards.mk). Generation runs at parse time so the include exists before
# the first .o. embed_www.py leaves the file untouched when the bytes match.

ifndef PM_METAL_WWW_MK
PM_METAL_WWW_MK := 1

PM_METAL_WWW_TOOLS := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
PM_METAL_WWW_ROOT := $(patsubst %/,%,$(dir $(PM_METAL_WWW_TOOLS)))
PM_METAL_WWW_INC := $(PM_METAL_WWW_ROOT)/src/pymergetic/metal/inspect/www_embed.inc.h
PM_METAL_WWW_DIR := $(PM_METAL_WWW_ROOT)/src/pymergetic/metal/inspect/www

ifneq ($(MAKECMDGOALS),clean)
PM_METAL_WWW_FAIL := $(shell python3 $(PM_METAL_WWW_TOOLS)/embed_www.py -o $(PM_METAL_WWW_INC) $(PM_METAL_WWW_DIR) || echo fail)
ifneq ($(PM_METAL_WWW_FAIL),)
$(error inspect www embed failed — run $(PM_METAL_WWW_TOOLS)/embed_www.py)
endif
endif

endif
