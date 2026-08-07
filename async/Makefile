# Host floor smoke: TLSF mem + cooperative async (no QEMU).
METALMOD := $(abspath ..)
INC := -I$(METALMOD)/include -I$(METALMOD)/third_party/tlsf
CFLAGS ?= -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
CC ?= gcc

SRCS := \
	$(METALMOD)/mem/mem.c \
	$(METALMOD)/third_party/tlsf/tlsf.c \
	$(METALMOD)/async/async.c \
	$(METALMOD)/async/time_host.c \
	host_smoke.c

.PHONY: all run clean
all: host_smoke
	./host_smoke

host_smoke: $(SRCS)
	$(CC) $(CFLAGS) $(INC) -o $@ $(SRCS)

run: all

clean:
	rm -f host_smoke
