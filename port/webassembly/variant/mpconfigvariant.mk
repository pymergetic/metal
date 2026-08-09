# metal CDN `mp`: wasmmod browser flags + live C boot (same as box seats).
MICROPY_PY_WASM = 1
WASMMOD_EMSCRIPTEN = 1

JSFLAGS += -s ASYNCIFY
JSFLAGS += -s ASYNCIFY_STACK_SIZE=65536
# CDN heap selector (up to 128 MiB GC) needs growable linear memory.
JSFLAGS += -s ALLOW_MEMORY_GROWTH

CFLAGS += -DMICROPY_WASM_HTTP_NATIVE=0

MICROPY_PY_SSL = 1
MICROPY_SSL_MBEDTLS = 1
MICROPY_WASM_VERIFY ?= 1
MICROPY_WASM_TRUST_CA ?= $(TOP)/extmod/wasmmod/examples/.keys/trust/root.crt.der

METAL := $(TOP)/extmod/metal
MICROPY_MANIFEST_METAL := $(METAL)
FROZEN_MANIFEST := $(METAL)/port/manifest_wasm.py

# Nested pymergetic.metal.* (wasmmod owns root; metal child nested in wasmmod.c).
CFLAGS += -DMICROPY_MODULE_BUILTIN_SUBPACKAGES=1

# Do not -I port/upy (ships mpconfigport.h and would shadow webassembly).
METAL_CDN_URL ?= https://cdn.pymergetic.com/cdn
METAL_CDN_EXTRA_URLS ?=

CFLAGS += -I$(METAL)/include -I$(METAL)/port/hal \
	-I$(METAL)/src/pymergetic/metal/fs/littlefs \
	-I$(METAL)/src/pymergetic/metal/fs/littlefs/vendor \
	-DLFS_CONFIG=lfs_config.h -DPM_METAL_LFS_FREESTANDING \
	-I$(METAL)/port/boot -I$(METAL)/glue \
	-I$(METAL)/src \
	-I$(METAL)/src/pymergetic/metal/util \
	-I$(METAL)/third_party/tlsf \
	-I$(METAL)/third_party/monocypher \
	-I$(METAL)/third_party/sha256 \
	-DPM_METAL_CFG_ARCH_WASM=1 -DPM_METAL_CFG_FW_BROWSER=1 \
	-DMETAL_CDN_URL=\"$(METAL_CDN_URL)\" \
	-DMETAL_CDN_EXTRA_URLS=\"$(METAL_CDN_EXTRA_URLS)\"

# Live boot + CORE faces via variant/extra_src.mk.
# Absolute paths for METAL_BOOT_SRCS; SRC_QSTR for nest qstrs.
METAL_BOOT_SRCS := \
	$(METAL)/port/boot/boot.c \
	$(METAL)/port/boot/autoexec.c \
	$(METAL)/port/boot/cdn_cfg.c \
	$(METAL)/glue/pymergetic/metal/__init__.c \
	$(METAL)/glue/pymergetic/metal/externals.c \
	$(METAL)/glue/pymergetic/metal/auth.c \
	$(METAL)/glue/pymergetic/metal/trust.c \
	$(METAL)/glue/pymergetic/metal/async.c \
	$(METAL)/glue/pymergetic/metal/process.c \
	$(METAL)/glue/pymergetic/metal/reg.c \
	$(METAL)/src/pymergetic/metal/reg/seats.c \
	$(METAL)/glue/pymergetic/metal/console.c \
	$(METAL)/glue/pymergetic/metal/rt.c \
	$(METAL)/glue/pymergetic/metal/boot/__init__.c \
	$(METAL)/glue/pymergetic/metal/boot/tree.c \
	$(METAL)/glue/pymergetic/metal/util/__init__.c \
	$(METAL)/glue/pymergetic/metal/util/lz4.c \
	$(METAL)/glue/pymergetic/metal/util/size.c \
	$(METAL)/glue/pymergetic/metal/util/endian.c \
	$(METAL)/glue/pymergetic/metal/util/fourcc.c \
	$(METAL)/glue/pymergetic/metal/util/eightcc.c \
	$(METAL)/glue/pymergetic/metal/util/ascii.c \
	$(METAL)/glue/pymergetic/metal/util/tar.c \
	$(METAL)/glue/pymergetic/metal/mem/__init__.c \
	$(METAL)/glue/pymergetic/metal/mem/port.c \
	$(METAL)/glue/pymergetic/metal/mem/tlsf.c \
	$(METAL)/glue/pymergetic/metal/mem/arena.c \
	$(METAL)/glue/pymergetic/metal/mem/lock.c \
	$(METAL)/port/hal/wasm/console.c \
	$(METAL)/port/hal/wasm/metal_log.c \
	$(METAL)/port/hal/wasm/mem.c \
	$(METAL)/port/hal/wasm/board_time.c \
	$(METAL)/port/hal/wasm/smp_stub.c \
	$(METAL)/port/hal/wasm/rt_block.c \
	$(METAL)/glue/pymergetic/metal/fs/__init__.c \
	$(METAL)/glue/pymergetic/metal/fs/embed.c \
	$(METAL)/glue/pymergetic/metal/fs/fat.c \
	$(METAL)/glue/pymergetic/metal/fs/littlefs.c \
	$(METAL)/glue/pymergetic/metal/fs/mtar.c \
	$(METAL)/glue/pymergetic/metal/fs/overlay.c \
	$(METAL)/glue/pymergetic/metal/fs/tmpfs.c \
	$(METAL)/glue/pymergetic/metal/fs/vfs.c \
	$(METAL)/glue/pymergetic/metal/fs/wasmmod.c \
	$(METAL)/glue/pymergetic/metal/fs/zip.c \
	$(METAL)/glue/pymergetic/metal/pack.c \
	$(METAL)/glue/pymergetic/metal/hwtree.c \
	$(METAL)/glue/pymergetic/metal/net/__init__.c \
	$(METAL)/glue/pymergetic/metal/net/asgi.c \
	$(METAL)/glue/pymergetic/metal/net/dhcp.c \
	$(METAL)/glue/pymergetic/metal/net/dns.c \
	$(METAL)/glue/pymergetic/metal/net/faces.c \
	$(METAL)/glue/pymergetic/metal/net/http.c \
	$(METAL)/glue/pymergetic/metal/net/ip.c \
	$(METAL)/glue/pymergetic/metal/net/nic.c \
	$(METAL)/glue/pymergetic/metal/net/ntp.c \
	$(METAL)/glue/pymergetic/metal/net/pump.c \
	$(METAL)/glue/pymergetic/metal/net/ssh.c \
	$(METAL)/glue/pymergetic/metal/net/tftp.c \
	$(METAL)/glue/pymergetic/metal/net/tls.c \
	$(METAL)/glue/pymergetic/metal/net/wg.c \
	$(METAL)/port/hal/wasm/net_http_fetch.c \
	$(METAL)/port/hal/wasm/net_pump_thin.c \
	$(METAL)/port/hal/wasm/net_ip_thin.c \
	$(METAL)/port/hal/wasm/net_dns_doh.c \
	$(METAL)/port/hal/wasm/net_ntp_wall.c \
	$(METAL)/port/hal/wasm/net_dhcp_stub.c \
	$(METAL)/port/hal/wasm/net_tftp_stub.c \
	$(METAL)/port/hal/wasm/net_tls_stub.c \
	$(METAL)/port/hal/wasm/net_ssh_stub.c \
	$(METAL)/port/hal/wasm/net_wg_stub.c \
	$(METAL)/port/hal/wasm/net_asgi_stub.c \
	$(METAL)/glue/pymergetic/metal/bus/__init__.c \
	$(METAL)/glue/pymergetic/metal/bus/pci.c \
	$(METAL)/glue/pymergetic/metal/bus/virtio.c \
	$(METAL)/glue/pymergetic/metal/dev/__init__.c \
	$(METAL)/glue/pymergetic/metal/dev/serial.c \
	$(METAL)/glue/pymergetic/metal/dev/acpi.c \
	$(METAL)/glue/pymergetic/metal/dev/blk.c \
	$(METAL)/glue/pymergetic/metal/dev/stream.c \
	$(METAL)/glue/pymergetic/metal/dev/gfx/__init__.c \
	$(METAL)/glue/pymergetic/metal/dev/gfx/compositor.c \
	$(METAL)/glue/pymergetic/metal/dev/gfx/scanout.c \
	$(METAL)/glue/pymergetic/metal/dev/gfx/text.c \
	$(METAL)/glue/pymergetic/metal/dev/input/__init__.c \
	$(METAL)/glue/pymergetic/metal/dev/input/kbd.c \
	$(METAL)/glue/pymergetic/metal/dev/net/__init__.c \
	$(METAL)/glue/pymergetic/metal/dev/net/bge.c \
	$(METAL)/glue/pymergetic/metal/dev/net/virtio_net.c \
	$(METAL)/glue/pymergetic/metal/shell/__init__.c \
	$(METAL)/glue/pymergetic/metal/shell/tui.c \
	$(METAL)/glue/pymergetic/metal/shell/ui.c \
	$(METAL)/glue/pymergetic/metal/shell/vt.c \
	$(METAL)/glue/pymergetic/metal/draw.c \
	$(METAL)/glue/pymergetic/metal/wamr_host.c \
	$(METAL)/port/hal/wasm/bus_pci_stub.c \
	$(METAL)/port/hal/wasm/bus_virtio_stub.c \
	$(METAL)/port/hal/wasm/dev_acpi_stub.c \
	$(METAL)/port/hal/wasm/kbd_stub.c \
	$(METAL)/port/hal/wasm/dev_serial.c \
	$(METAL)/port/hal/wasm/dev_net_bge_stub.c \
	$(METAL)/port/hal/wasm/dev_net_virtio_stub.c \
	$(METAL)/port/hal/wasm/gfx_stub.c \
	$(METAL)/port/hal/wasm/shell_ui_stub.c \
	$(METAL)/src/pymergetic/metal/draw/__init__.c \
	$(METAL)/src/pymergetic/metal/shell/vt/__init__.c \
	$(METAL)/src/pymergetic/metal/shell/tui/__init__.c \
	$(METAL)/src/pymergetic/metal/pack/mod_packs.c \
	$(METAL)/src/pymergetic/metal/net/faces/__init__.c \
	$(METAL)/src/pymergetic/metal/net/nic/__init__.c \
	$(METAL)/src/pymergetic/metal/boot/tree.c \
	$(METAL)/src/pymergetic/metal/boot/externals.c \
	$(METAL)/src/pymergetic/metal/boot/externals_rows.c \
	$(METAL)/src/pymergetic/metal/arch/arch.c \
	$(METAL)/src/pymergetic/metal/arch/py_call.c \
	$(METAL)/src/pymergetic/metal/arch/wasm/bridge.c \
	$(METAL)/src/pymergetic/metal/arch/x86/bridge.c \
	$(METAL)/src/pymergetic/metal/arch/x86_64/bridge.c \
	$(METAL)/src/pymergetic/metal/unix/x86/bridge.c \
	$(METAL)/src/pymergetic/metal/unix/x86_64/bridge.c \
	$(METAL)/src/pymergetic/metal/net/microdot/bridge.c \
	$(METAL)/src/pymergetic/metal/auth/__init__.c \
	$(METAL)/src/pymergetic/metal/trust/__init__.c \
	$(METAL)/src/pymergetic/metal/async/__init__.c \
	$(METAL)/src/pymergetic/metal/async/meter.c \
	$(METAL)/src/pymergetic/metal/process/__init__.c \
	$(METAL)/src/pymergetic/metal/boot/unboot.c \
	$(METAL)/src/pymergetic/metal/console/__init__.c \
	$(METAL)/src/pymergetic/metal/util/ascii.c \
	$(METAL)/src/pymergetic/metal/util/endian/__init__.c \
	$(METAL)/src/pymergetic/metal/util/fourcc/__init__.c \
	$(METAL)/src/pymergetic/metal/util/eightcc/__init__.c \
	$(METAL)/src/pymergetic/metal/mem/port/mem.c \
	$(METAL)/src/pymergetic/metal/dev/stream/__init__.c \
	$(METAL)/port/hal/wasm/dev_blk.c \
	$(METAL)/port/hal/wasm/wamr_host.c \
	$(METAL)/src/pymergetic/metal/fs/littlefs/_glue.c \
	$(METAL)/src/pymergetic/metal/fs/littlefs/vendor/lfs.c \
	$(METAL)/third_party/tlsf/tlsf.c \
	$(METAL)/third_party/monocypher/monocypher.c \
	$(METAL)/third_party/monocypher/monocypher-ed25519.c \
	$(METAL)/third_party/sha256/sha256.c

# Attribute qstrs from metal glue faces (dotted module names come from these TUs).
SRC_QSTR += \
	$(METAL)/glue/pymergetic/metal/__init__.c \
	$(METAL)/glue/pymergetic/metal/externals.c \
	$(METAL)/glue/pymergetic/metal/auth.c \
	$(METAL)/glue/pymergetic/metal/trust.c \
	$(METAL)/glue/pymergetic/metal/async.c \
	$(METAL)/glue/pymergetic/metal/process.c \
	$(METAL)/glue/pymergetic/metal/reg.c \
	$(METAL)/src/pymergetic/metal/reg/seats.c \
	$(METAL)/glue/pymergetic/metal/console.c \
	$(METAL)/glue/pymergetic/metal/rt.c \
	$(METAL)/glue/pymergetic/metal/boot/__init__.c \
	$(METAL)/glue/pymergetic/metal/boot/tree.c \
	$(METAL)/glue/pymergetic/metal/util/__init__.c \
	$(METAL)/glue/pymergetic/metal/util/lz4.c \
	$(METAL)/glue/pymergetic/metal/util/size.c \
	$(METAL)/glue/pymergetic/metal/util/endian.c \
	$(METAL)/glue/pymergetic/metal/util/fourcc.c \
	$(METAL)/glue/pymergetic/metal/util/eightcc.c \
	$(METAL)/glue/pymergetic/metal/util/ascii.c \
	$(METAL)/glue/pymergetic/metal/util/tar.c \
	$(METAL)/glue/pymergetic/metal/mem/__init__.c \
	$(METAL)/glue/pymergetic/metal/mem/port.c \
	$(METAL)/glue/pymergetic/metal/mem/tlsf.c \
	$(METAL)/glue/pymergetic/metal/mem/arena.c \
	$(METAL)/glue/pymergetic/metal/mem/lock.c \
	$(METAL)/glue/pymergetic/metal/fs/__init__.c \
	$(METAL)/glue/pymergetic/metal/fs/embed.c \
	$(METAL)/glue/pymergetic/metal/fs/fat.c \
	$(METAL)/glue/pymergetic/metal/fs/littlefs.c \
	$(METAL)/glue/pymergetic/metal/fs/mtar.c \
	$(METAL)/glue/pymergetic/metal/fs/overlay.c \
	$(METAL)/glue/pymergetic/metal/fs/tmpfs.c \
	$(METAL)/glue/pymergetic/metal/fs/vfs.c \
	$(METAL)/glue/pymergetic/metal/fs/wasmmod.c \
	$(METAL)/glue/pymergetic/metal/fs/zip.c \
	$(METAL)/glue/pymergetic/metal/pack.c \
	$(METAL)/glue/pymergetic/metal/hwtree.c \
	$(METAL)/glue/pymergetic/metal/net/__init__.c \
	$(METAL)/glue/pymergetic/metal/net/asgi.c \
	$(METAL)/glue/pymergetic/metal/net/dhcp.c \
	$(METAL)/glue/pymergetic/metal/net/dns.c \
	$(METAL)/glue/pymergetic/metal/net/faces.c \
	$(METAL)/glue/pymergetic/metal/net/http.c \
	$(METAL)/glue/pymergetic/metal/net/ip.c \
	$(METAL)/glue/pymergetic/metal/net/nic.c \
	$(METAL)/glue/pymergetic/metal/net/ntp.c \
	$(METAL)/glue/pymergetic/metal/net/pump.c \
	$(METAL)/glue/pymergetic/metal/net/ssh.c \
	$(METAL)/glue/pymergetic/metal/net/tftp.c \
	$(METAL)/glue/pymergetic/metal/net/tls.c \
	$(METAL)/glue/pymergetic/metal/net/wg.c \
	$(METAL)/glue/pymergetic/metal/bus/__init__.c \
	$(METAL)/glue/pymergetic/metal/bus/pci.c \
	$(METAL)/glue/pymergetic/metal/bus/virtio.c \
	$(METAL)/glue/pymergetic/metal/dev/__init__.c \
	$(METAL)/glue/pymergetic/metal/dev/serial.c \
	$(METAL)/glue/pymergetic/metal/dev/acpi.c \
	$(METAL)/glue/pymergetic/metal/dev/blk.c \
	$(METAL)/glue/pymergetic/metal/dev/stream.c \
	$(METAL)/glue/pymergetic/metal/dev/gfx/__init__.c \
	$(METAL)/glue/pymergetic/metal/dev/gfx/compositor.c \
	$(METAL)/glue/pymergetic/metal/dev/gfx/scanout.c \
	$(METAL)/glue/pymergetic/metal/dev/gfx/text.c \
	$(METAL)/glue/pymergetic/metal/dev/input/__init__.c \
	$(METAL)/glue/pymergetic/metal/dev/input/kbd.c \
	$(METAL)/glue/pymergetic/metal/dev/net/__init__.c \
	$(METAL)/glue/pymergetic/metal/dev/net/bge.c \
	$(METAL)/glue/pymergetic/metal/dev/net/virtio_net.c \
	$(METAL)/glue/pymergetic/metal/shell/__init__.c \
	$(METAL)/glue/pymergetic/metal/shell/tui.c \
	$(METAL)/glue/pymergetic/metal/shell/ui.c \
	$(METAL)/glue/pymergetic/metal/shell/vt.c \
	$(METAL)/glue/pymergetic/metal/draw.c \
	$(METAL)/glue/pymergetic/metal/wamr_host.c
