# Standalone prove for the mrustc in-process embed (Rust -> C, no subprocess).
#
#   make -C extmod/metal/tools/mrustc_embed prove
#
# Links the shim + prove harness against the vendored mrustc.a (which already
# contains every compile pass; only main.o is excluded) and runs it on a
# #![no_core] input. This proves the JIT lives *inside* the binary before the
# card is wired to it.

MRUSTC_DIR ?= $(abspath ../../externals/mrustc)
CXX ?= g++
CC ?= cc
OBJDIR := build

MRUSTC_A := $(MRUSTC_DIR)/bin/mrustc.a
COMMON_LIB_A := $(MRUSTC_DIR)/bin/common_lib.a

CXXFLAGS := -std=c++14 -O2 -g -Wall -Wno-unused-parameter -Wno-sign-compare \
	-I $(MRUSTC_DIR)/src/include \
	-I $(MRUSTC_DIR)/src \
	-I $(MRUSTC_DIR)/tools/common \
	-I .

# mrustc.a is built with -Wl,--whole-archive so every pass object is retained.
# The embed shim is the *only* client of most of those symbols, so it needs the
# same whole-archive treatment or the linker drops the passes.
LDFLAGS := -g -Wl,--whole-archive $(MRUSTC_A) -Wl,--no-whole-archive \
	$(COMMON_LIB_A) -lz -lpthread -ldl

.PHONY: prove clean

prove: $(OBJDIR)/mrustc_embed_prove
	$(OBJDIR)/mrustc_embed_prove

$(OBJDIR)/mrustc_embed_prove: mrustc_embed.cpp prove.c mrustc_embed.h $(MRUSTC_A) $(COMMON_LIB_A)
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c -o $(OBJDIR)/mrustc_embed.o mrustc_embed.cpp
	$(CC) -std=gnu11 -O2 -g -I . -c -o $(OBJDIR)/prove.o prove.c
	$(CXX) -o $@ $(OBJDIR)/prove.o $(OBJDIR)/mrustc_embed.o $(LDFLAGS)

clean:
	rm -rf $(OBJDIR)
