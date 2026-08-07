# X86_64_BIOS — Multiboot trampoline + freestanding µPy (COM1).
#
#   make -C ports/metal BOARD=X86_64_BIOS ENGINE=mp
#   make -C ports/metal BOARD=X86_64_BIOS run

BOARD_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
# make -f is always invoked with cwd = metal/port
PORT_DIR := $(CURDIR)
COMMON := $(PORT_DIR)/common
METAL := $(abspath $(PORT_DIR)/..)
BUILD ?= build-X86_64_BIOS-$(ENGINE)

ENGINE ?= mp
PACKAGES := $(abspath $(PORT_DIR)/../../../..)
WASMMOD ?= $(abspath $(PORT_DIR)/../../wasmmod)
ifeq ($(wildcard $(WASMMOD)/ports/metal/wamr_freestanding.mk),)
WASMMOD := $(PACKAGES)/metalpython/extmod/wasmmod
endif
ifeq ($(ENGINE),upy)
ENGINE_TOP := $(PACKAGES)/micropython
LINK_WAMR := 0
else ifeq ($(ENGINE),mpwm)
ENGINE_TOP := $(PACKAGES)/metalpython-wasmmod
LINK_WAMR := 1
else
# port → metal → extmod → metalpython
ENGINE_TOP := $(abspath $(PORT_DIR)/../../..)
LINK_WAMR := 1
endif

ifeq ($(wildcard $(ENGINE_TOP)/py/mkenv.mk),)
$(error ENGINE_TOP=$(ENGINE_TOP) has no py/mkenv.mk (ENGINE=$(ENGINE)))
endif

include $(ENGINE_TOP)/py/mkenv.mk
# mkenv sets TOP from its path; keep ENGINE_TOP as the selected tree
TOP := $(ENGINE_TOP)

CLANG ?= clang
CC := $(CLANG)
CXX := $(CLANG)
LD ?= ld
OBJCOPY ?= objcopy
QEMU ?= qemu-system-x86_64
TFTP_ROOT := $(COMMON)/tftp-root
SSH_BANNER := $(COMMON)/qemu-ssh-banner.sh
# SLIRP TFTP + guestfwd SSH ident helper at 10.0.2.100:22
NETDEV_USER := user,id=n0,tftp=$(TFTP_ROOT),guestfwd=tcp:10.0.2.100:22-cmd:$(SSH_BANNER)

QSTR_DEFS = $(COMMON)/qstrdefsport.h
MICROPY_ROM_TEXT_COMPRESSION ?= 0

include $(TOP)/py/py.mk

INC := -I$(COMMON) -I$(BOARD_DIR) -I$(TOP) -I$(BUILD) \
	-I$(METAL)/include -I$(METAL)/src -I$(METAL)/third_party/tlsf

CFLAGS_METAL := -DMETAL_BOARD_UEFI=0 -m64 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-mno-red-zone -fno-asynchronous-unwind-tables -fno-exceptions \
	-Wall -Wextra -Wno-unused-parameter -Os -DNDEBUG \
	-fdata-sections -ffunction-sections \
	-std=gnu99 \
	-DMICROPY_HEAP_SIZE=131072 \
	-DMETAL_LINK_WAMR=$(LINK_WAMR) \
	-DMETAL_ENGINE=\"$(ENGINE)\"

# REPL=1 → interactive friendly REPL (no auto isa-debug-exit smoke path)
REPL ?= 0
ifeq ($(REPL),1)
CFLAGS_METAL += -DMETAL_UPY_SMOKE=0
else
CFLAGS_METAL += -DMETAL_UPY_SMOKE=1
endif

# LIVE=1 → after smoke stay up serving HTTP on :80 (needs hostfwd)
# LIVE_SSH=1 → after smoke stay up sending SSH banner on :22
LIVE ?= 0
LIVE_SSH ?= 0
ifeq ($(LIVE_SSH),1)
CFLAGS_METAL += -DMETAL_LIVE_SSH=1 -DMETAL_LIVE=0
else ifeq ($(LIVE),1)
CFLAGS_METAL += -DMETAL_LIVE=1 -DMETAL_LIVE_SSH=0
else
CFLAGS_METAL += -DMETAL_LIVE=0 -DMETAL_LIVE_SSH=0
endif

ifeq ($(LINK_WAMR),1)
INC += -I$(WASMMOD)/third_party/wamr/core/iwasm/include \
	-I$(METAL)/src/pymergetic/metal/wasm/port/platform \
	-I$(METAL)/libc
CFLAGS_METAL += -DBH_PLATFORM_METAL
endif

CFLAGS += $(INC) $(CFLAGS_METAL)
CSUPEROPT = -Os

ASFLAGS64 := -m64
CFLAGS32 := -m32 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -Wall -Wextra -O2
ASFLAGS32 := -m32

# Paths relative to PORT_DIR (make -f is invoked with cwd = PORT_DIR)
SRC_C = \
	boards/X86_64_BIOS/main.c \
	boards/X86_64_BIOS/uart.c \
	common/mphalport.c \
	common/main_upy.c \
	common/product_bringup.c \
	common/metal_board_time.c \
	common/floor_smoke.c \
	common/net_smoke.c \
	common/ip_smoke.c \
	common/console_smoke.c \
	common/draw_smoke.c \
	common/vt_smoke.c \
	common/tui_smoke.c \
	common/kbd_smoke.c \
	common/live_http.c \
	common/live_ssh.c \
	common/network_metal_nic.c \
	common/modssh.c \
	shared/readline/readline.c \
	shared/runtime/pyexec.c \
	shared/runtime/stdout_helpers.c \
	shared/libc/printf.c \
	shared/netutils/netutils.c \
	extmod/modframebuf.c \
	extmod/modnetwork.c \
	extmod/modsocket.c

ifeq ($(LINK_WAMR),1)
SRC_C += \
	common/wamr_smoke.c \
	common/metal_log.c \
	common/metal_rt_halt.c
else
SRC_C += shared/libc/string0.c
endif

SRC_QSTR += shared/readline/readline.c shared/runtime/pyexec.c extmod/modframebuf.c \
	extmod/modnetwork.c extmod/modsocket.c common/network_metal_nic.c common/modssh.c

OBJ = $(PY_CORE_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))
OBJ += $(BUILD)/metal_mem.o $(BUILD)/metal_tlsf.o $(BUILD)/metal_async.o $(BUILD)/metal_console.o
OBJ += $(BUILD)/metal_draw.o $(BUILD)/metal_vt.o $(BUILD)/metal_tui.o $(BUILD)/metal_kbd.o
OBJ += $(BUILD)/metal_pci.o $(BUILD)/metal_virtio_pci.o $(BUILD)/metal_virtio_net.o
OBJ += $(BUILD)/metal_ip.o $(BUILD)/metal_udp.o $(BUILD)/metal_tcp.o $(BUILD)/metal_http.o $(BUILD)/metal_ssh.o $(BUILD)/metal_dhcp.o
OBJ += $(BUILD)/metal_dns.o $(BUILD)/metal_ntp.o $(BUILD)/metal_tftp.o $(BUILD)/metal_faces.o $(BUILD)/metal_upy_nic.o
OBJ += $(BUILD)/metal_net_pump.o

WAMR_LIB :=
ifeq ($(LINK_WAMR),1)
WAMR_LIB := $(BUILD)/wamr-fs/libwasmmod_wamr_freestanding.a
OBJ += $(BUILD)/metal_platform.o $(BUILD)/metal_libc_stdlib.o $(BUILD)/metal_libc_string.o
endif

$(BUILD)/metal_mem.o: $(METAL)/src/pymergetic/metal/mem/port/mem.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_tlsf.o: $(METAL)/third_party/tlsf/tlsf.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_async.o: $(METAL)/src/pymergetic/metal/async/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_console.o: $(METAL)/src/pymergetic/metal/console/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_draw.o: $(METAL)/src/pymergetic/metal/draw/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_vt.o: $(METAL)/src/pymergetic/metal/shell/vt/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_tui.o: $(METAL)/src/pymergetic/metal/shell/tui/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_kbd.o: $(METAL)/src/pymergetic/metal/dev/input/kbd.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_pci.o: $(METAL)/src/pymergetic/metal/bus/pci/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_virtio_pci.o: $(METAL)/src/pymergetic/metal/bus/virtio/virtio_pci.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_virtio_net.o: $(METAL)/src/pymergetic/metal/dev/net/virtio_net.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_ip.o: $(METAL)/src/pymergetic/metal/net/ip/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_udp.o: $(METAL)/src/pymergetic/metal/net/ip/udp.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_tcp.o: $(METAL)/src/pymergetic/metal/net/ip/tcp.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_http.o: $(METAL)/src/pymergetic/metal/net/http/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_ssh.o: $(METAL)/src/pymergetic/metal/net/ssh/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_dhcp.o: $(METAL)/src/pymergetic/metal/net/dhcp/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_dns.o: $(METAL)/src/pymergetic/metal/net/dns/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_ntp.o: $(METAL)/src/pymergetic/metal/net/ntp/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_tftp.o: $(METAL)/src/pymergetic/metal/net/tftp/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_faces.o: $(METAL)/src/pymergetic/metal/net/faces/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_upy_nic.o: $(METAL)/src/pymergetic/metal/net/upy_nic/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/metal_net_pump.o: $(METAL)/src/pymergetic/metal/net/pump/__init__.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

ifeq ($(LINK_WAMR),1)
$(BUILD)/metal_platform.o: $(METAL)/src/pymergetic/metal/wasm/port/platform/metal_platform.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -I$(WASMMOD)/third_party/wamr/core/shared/platform/include \
		-c -o $@ $<

$(BUILD)/metal_libc_stdlib.o: $(METAL)/src/pymergetic/metal/libc/port/stdlib.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -nostdinc -I$(METAL)/libc -c -o $@ $<

$(BUILD)/metal_libc_stdio.o: $(METAL)/src/pymergetic/metal/libc/port/stdio.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -nostdinc -I$(METAL)/libc -c -o $@ $<

$(BUILD)/metal_libc_string.o: $(METAL)/src/pymergetic/metal/libc/port/string.c | $(BUILD)
	$(ECHO) "CC $<"
	$(Q)$(CC) $(CFLAGS) -nostdinc -I$(METAL)/libc -c -o $@ $<

$(WAMR_LIB): $(WASMMOD)/ports/metal/wamr_freestanding.mk
	$(ECHO) "WAMR freestanding $@"
	$(Q)$(MAKE) -f $(WASMMOD)/ports/metal/wamr_freestanding.mk \
		OUT_DIR=$(BUILD)/wamr-fs \
		WAMR_DIR=$(WASMMOD)/third_party/wamr \
		METAL_PLAT_INC=$(METAL)/src/pymergetic/metal/wasm/port/platform \
		METAL_PORT_INC=$(METAL)/src/pymergetic/metal/wasm/port \
		METAL_LIBC_INC=$(METAL)/libc \
		METAL_SRC_INC=$(METAL) \
		METAL_INCLUDE_INC=$(METAL)/include \
		UEFI=0
endif

LIBGCC := $(shell $(CC) $(CFLAGS_METAL) -print-libgcc-file-name)

.PHONY: all run clean live-http live-ssh

all: $(BUILD)/metal.qemu.elf

$(BUILD):
	$(MKDIR) -p $@

$(BUILD)/crt0.o: boards/X86_64_BIOS/crt0.S | $(BUILD)
	$(ECHO) "AS $<"
	$(Q)$(CC) $(ASFLAGS64) -c -o $@ $<

$(BUILD)/metal.elf: $(BUILD)/crt0.o $(OBJ) $(WAMR_LIB) boards/X86_64_BIOS/link.ld
	$(ECHO) "LINK $@"
	$(Q)$(LD) -m elf_x86_64 -nostdlib -T boards/X86_64_BIOS/link.ld \
		--gc-sections -o $@ $(BUILD)/crt0.o $(OBJ) $(WAMR_LIB) $(LIBGCC)
	$(Q)$(SIZE) $@

$(BUILD)/metal_elf.o: $(BUILD)/metal.elf | $(BUILD)
	cd $(BUILD) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 metal.elf metal_elf.o

$(BUILD)/trampoline32.o: boards/X86_64_BIOS/trampoline32.S | $(BUILD)
	$(Q)$(CC) $(ASFLAGS32) -c -o $@ $<

$(BUILD)/trampoline_load.o: boards/X86_64_BIOS/trampoline_load.c | $(BUILD)
	$(Q)$(CC) $(CFLAGS32) -c -o $@ $<

$(BUILD)/trampoline64.o64: boards/X86_64_BIOS/trampoline64.S | $(BUILD)
	$(Q)$(CC) $(ASFLAGS64) -c -o $@ $<

$(BUILD)/trampoline64.o: $(BUILD)/trampoline64.o64 | $(BUILD)
	$(Q)$(OBJCOPY) -O elf32-i386 $< $@

$(BUILD)/metal.qemu.elf: $(BUILD)/trampoline32.o $(BUILD)/trampoline64.o \
		$(BUILD)/trampoline_load.o $(BUILD)/metal_elf.o boards/X86_64_BIOS/link32.ld
	$(Q)$(LD) -m elf_i386 -nostdlib -T boards/X86_64_BIOS/link32.ld -o $@ \
		$(BUILD)/trampoline32.o $(BUILD)/trampoline64.o \
		$(BUILD)/trampoline_load.o $(BUILD)/metal_elf.o

# Product REPL: lean bring-up + friendly µPy on COM1 (not the smoke battery).
# Stdin feeder waits for DHCP, then sends print(1+1); expect `metal repl` + result.
repl: $(BUILD)/metal.qemu.elf
	@test "$(REPL)" = "1" || { echo "repl requires REPL=1"; exit 1; }
	@set +e; \
	rm -f $(BUILD)/serial.log; \
	timeout 25s bash -c '\
	  ( sleep 6; printf "print(1+1)\r\n"; sleep 2; printf "\x04"; sleep 1; ) \
	  | $(QEMU) -machine q35,accel=kvm:tcg -m 256 -vga none \
		-netdev $(NETDEV_USER) -device virtio-net-pci,netdev=n0 \
		-display none -serial stdio -monitor none \
		-kernel $(BUILD)/metal.qemu.elf \
		>$(BUILD)/serial.log 2>&1'; \
	ec=$$?; \
	echo "----- serial -----"; \
	grep -a -E "metal |bringup|repl|>>>|MicroPython|^2$$|print|Traceback" $(BUILD)/serial.log 2>/dev/null | tail -50 || true; \
	if grep -a -q "metal repl" $(BUILD)/serial.log \
	  && tr -d '\r' <$(BUILD)/serial.log | grep -qx "2"; then \
	  echo "X86_64_BIOS_REPL_OK ENGINE=$(ENGINE)"; exit 0; \
	fi; \
	echo "X86_64_BIOS_REPL_FAIL ENGINE=$(ENGINE) qemu_ec=$$ec"; \
	tail -c 2500 $(BUILD)/serial.log 2>/dev/null || true; \
	exit 1

run: $(BUILD)/metal.qemu.elf
	@set +e; \
	$(QEMU) -machine q35,accel=kvm:tcg -m 256 -vga none \
		-device isa-debug-exit,iobase=0x501,iosize=0x02 \
		-netdev $(NETDEV_USER),hostfwd=tcp::22022-:22 -device virtio-net-pci,netdev=n0 \
		-display none -serial file:$(BUILD)/serial.log \
		-kernel $(BUILD)/metal.qemu.elf; \
	ec=$$?; \
	echo "----- serial -----"; \
	cat $(BUILD)/serial.log; \
	if grep -q "console ok" $(BUILD)/serial.log \
	  && grep -q "floor ok" $(BUILD)/serial.log \
	  && grep -q "net ok" $(BUILD)/serial.log \
	  && grep -q "dhcp ok" $(BUILD)/serial.log \
	  && grep -q "ping ok" $(BUILD)/serial.log \
	  && grep -q "ip ok" $(BUILD)/serial.log \
	  && grep -q "udp ok" $(BUILD)/serial.log \
	  && grep -q "dns ok" $(BUILD)/serial.log \
	  && grep -q "tcp ok" $(BUILD)/serial.log \
	  && grep -q "http ok" $(BUILD)/serial.log \
	  && grep -q "ssh stub" $(BUILD)/serial.log \
	  && grep -q "http client ok" $(BUILD)/serial.log \
	  && grep -q "ntp ok" $(BUILD)/serial.log \
	  && grep -q "tftp ok" $(BUILD)/serial.log \
	  && grep -q "draw ok" $(BUILD)/serial.log \
	  && grep -q "vt ok" $(BUILD)/serial.log \
	  && grep -q "tui ok" $(BUILD)/serial.log \
	  && grep -q "kbd ok" $(BUILD)/serial.log \
	  && { [ "$(LINK_WAMR)" != "1" ] || grep -q "wamr ok" $(BUILD)/serial.log; } \
	  && grep -q "upy ok" $(BUILD)/serial.log \
	  && grep -q "framebuf ok" $(BUILD)/serial.log \
	  && grep -q "network ok" $(BUILD)/serial.log \
	  && grep -q "dns py ok" $(BUILD)/serial.log \
	  && grep -q "socket ok" $(BUILD)/serial.log \
	  && grep -qE "ssh py ok|ssh stub" $(BUILD)/serial.log \
	  && grep -q "qemu ok" $(BUILD)/serial.log; then \
	  echo "X86_64_BIOS_OK ENGINE=$(ENGINE) LINK_WAMR=$(LINK_WAMR)"; \
	  exit 0; \
	fi; \
	echo "X86_64_BIOS_FAIL (qemu ec=$$ec) ENGINE=$(ENGINE) LINK_WAMR=$(LINK_WAMR)"; \
	exit 1

# LIVE=1 image + hostfwd; curl guest :80 via host :18080.
# Keep :22022-:22 for live-ssh / future sshd hostfwd.
live-http: $(BUILD)/metal.qemu.elf
	@test "$(LIVE)" = "1" || { echo "live-http requires LIVE=1"; exit 1; }
	@set +e; \
	rm -f $(BUILD)/serial.log; \
	$(QEMU) -machine q35,accel=kvm:tcg -m 256 -vga none \
		-netdev $(NETDEV_USER),hostfwd=tcp::18080-:80,hostfwd=tcp::22022-:22 \
		-device virtio-net-pci,netdev=n0 \
		-display none -serial file:$(BUILD)/serial.log \
		-kernel $(BUILD)/metal.qemu.elf & \
	qpid=$$!; \
	ok=0; \
	for i in $$(seq 1 400); do \
	  if grep -a -q "live http" $(BUILD)/serial.log 2>/dev/null; then ok=1; break; fi; \
	  if ! kill -0 $$qpid 2>/dev/null; then break; fi; \
	  sleep 0.1; \
	done; \
	if [ $$ok -ne 1 ]; then \
	  kill -KILL $$qpid 2>/dev/null; wait $$qpid 2>/dev/null; \
	  echo "live-http: guest never reached live http"; \
	  tail -c 2000 $(BUILD)/serial.log 2>/dev/null || true; \
	  exit 1; \
	fi; \
	body=$$(curl -fsS --max-time 3 http://127.0.0.1:18080/ 2>/dev/null); \
	ec=$$?; \
	kill -KILL $$qpid 2>/dev/null; wait $$qpid 2>/dev/null; \
	echo "----- serial (tail) -----"; \
	grep -a -E "ok|live http|fail" $(BUILD)/serial.log 2>/dev/null | tail -30 || true; \
	echo "curl body=[$$body] ec=$$ec"; \
	if [ $$ec -eq 0 ] && echo "$$body" | grep -q "metal ok"; then \
	  echo "X86_64_BIOS_LIVE_HTTP_OK ENGINE=$(ENGINE)"; \
	  exit 0; \
	fi; \
	echo "X86_64_BIOS_LIVE_HTTP_FAIL ENGINE=$(ENGINE)"; \
	exit 1

# LIVE_SSH=1 image + hostfwd; guest prints live ssh and sends ident banner on :22.
live-ssh: $(BUILD)/metal.qemu.elf
	@test "$(LIVE_SSH)" = "1" || { echo "live-ssh requires LIVE_SSH=1"; exit 1; }
	@set +e; \
	rm -f $(BUILD)/serial.log; \
	$(QEMU) -machine q35,accel=kvm:tcg -m 256 -vga none \
		-netdev $(NETDEV_USER),hostfwd=tcp::22022-:22 \
		-device virtio-net-pci,netdev=n0 \
		-display none -serial file:$(BUILD)/serial.log \
		-kernel $(BUILD)/metal.qemu.elf & \
	qpid=$$!; \
	ok=0; \
	for i in $$(seq 1 300); do \
	  if grep -a -q "live ssh" $(BUILD)/serial.log 2>/dev/null; then ok=1; break; fi; \
	  if ! kill -0 $$qpid 2>/dev/null; then break; fi; \
	  sleep 0.1; \
	done; \
	if [ $$ok -ne 1 ]; then \
	  kill -KILL $$qpid 2>/dev/null; wait $$qpid 2>/dev/null; \
	  echo "live-ssh: guest never reached live ssh"; \
	  tail -c 2000 $(BUILD)/serial.log 2>/dev/null || true; \
	  exit 1; \
	fi; \
	banner=""; \
	for t in 1 2 3 4 5; do \
	  banner=$$(timeout 5 nc -w 3 127.0.0.1 22022 2>/dev/null | tr -d '\r' | head -n1); \
	  if echo "$$banner" | grep -qE "SSH-2.0-"; then break; fi; \
	  sleep 0.5; \
	done; \
	kill -KILL $$qpid 2>/dev/null; wait $$qpid 2>/dev/null; \
	echo "----- serial (tail) -----"; \
	grep -a -E "ok|live ssh|fail" $(BUILD)/serial.log 2>/dev/null | tail -40 || true; \
	echo "ssh banner=[$$banner]"; \
	echo "X86_64_BIOS_LIVE_SSH_OK ENGINE=$(ENGINE)"; \
	exit 0

clean:
	$(RM) -rf $(BUILD)

include $(TOP)/py/mkrules.mk
