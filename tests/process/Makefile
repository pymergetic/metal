METAL := $(abspath ../..)
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -I$(METAL)/include -I$(METAL)/src

.PHONY: all clean
all: test_process_table test_unboot
	./test_process_table
	./test_unboot

test_process_table: test_process_table.c $(METAL)/src/pymergetic/metal/process/__init__.c
	$(CC) $(CFLAGS) -o $@ test_process_table.c $(METAL)/src/pymergetic/metal/process/__init__.c

test_unboot: test_unboot.c $(METAL)/src/pymergetic/metal/process/__init__.c $(METAL)/src/pymergetic/metal/boot/unboot.c
	$(CC) $(CFLAGS) -o $@ test_unboot.c \
		$(METAL)/src/pymergetic/metal/process/__init__.c \
		$(METAL)/src/pymergetic/metal/boot/unboot.c

clean:
	rm -f test_process_table test_unboot
