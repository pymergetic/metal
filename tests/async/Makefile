METAL := $(abspath ../..)
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2 \
	-I$(METAL)/include -I$(METAL)/src -I$(METAL)/third_party/tlsf

SRCS := \
	test_async_stress.c \
	$(METAL)/src/pymergetic/metal/async/__init__.c \
	$(METAL)/src/pymergetic/metal/async/meter.c \
	$(METAL)/src/pymergetic/metal/mem/port/mem.c \
	$(METAL)/third_party/tlsf/tlsf.c \
	$(METAL)/src/pymergetic/metal/process/__init__.c \
	$(METAL)/port/unix/variant/board_time.c \
	$(METAL)/port/unix/variant/smp_stub.c \
	$(METAL)/port/unix/variant/quiesce.c

.PHONY: all clean
all: test_async_stress
	./test_async_stress

test_async_stress: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -f test_async_stress
