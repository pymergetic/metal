/* mpconfigport.h — µPy config for the metal host seat's embedded kernel.
 *
 * The host C seat (host_test/ksweep/selfhost feeds) historically carried no
 * µPy, so pymergetic.metal.jit.py's object loop compiled to the polite
 * "no mpy artifact output on this seat" refusal and the three impl="py"
 * facade cards (util.pysample, wasmmod, wasmmod.net) refused in ksweep.
 *
 * This config builds µPy through the upstream embed port (ports/embed:
 * embed.mk emits micropython_embed/ — py core + this config + genhdr +
 * port/embed_util.c), the supported way to embed the µPy compiler+runtime
 * into a wider C project. The kernel is the host seat's *fill* for the jit.py
 * card's faces: the card's C face (object_compile/object_load) is unchanged,
 * only its capability is now backed by in-process µPy instead of a stub.
 *
 * Capability set — exactly what the object loop proves:
 *   - MICROPY_ENABLE_COMPILER: py/compile.c runs in-process (source -> mpy)
 *   - MICROPY_PERSISTENT_CODE_SAVE + LOAD: the mpy artifact bytes both ways
 *   - MICROPY_ENABLE_GC: module dicts/functions need the GC heap
 *   - core builtins (int/str/list/dict/tuple/exception), enough for the
 *     facade __init__.py bodies (annotations are compile-time only, their
 *     imports resolve at module *run* time which object_load pins)
 *   - sys + micropython builtins so module bodies can print/introspect
 *
 * Deliberately OFF (the browser seat pins the same posture):
 *   - MICROPY_PY_THREAD/GIL: every µPy entry on this binary is serialized by
 *     the async card's VM lock; MP_THREAD_GIL_TRYLOCK degrades to 1, the
 *     same no-op the browser cell runs with.
 *   - native emitters: no exec-heap marking, no arch-specific mpy feature
 *     bits beyond the portable bytecode the facades need.
 *
 * ROM level BASIC covers the core builtins without dragging extmod modules
 * into moduledefs.h (the host binary links its own cards — an embed table
 * naming wasmmod/metal builtins would leave dangling symbols).
 */
#ifndef METAL_INCLUDED_HOST_UPY_MPCONFIGPORT_H
#define METAL_INCLUDED_HOST_UPY_MPCONFIGPORT_H

/* the embed port's machine types + alloca + port header wiring */
#include <port/mpconfigport_common.h>

#define MICROPY_CONFIG_ROM_LEVEL                (MICROPY_CONFIG_ROM_LEVEL_BASIC_FEATURES)

/* the object loop: compile, save, load, execute */
#define MICROPY_ENABLE_COMPILER                 (1)
#define MICROPY_ENABLE_GC                       (1)
#define MICROPY_PERSISTENT_CODE_LOAD            (1)
#ifndef MICROPY_PERSISTENT_CODE_SAVE
#define MICROPY_PERSISTENT_CODE_SAVE            (1)
#endif

/* module bodies are small; the VM budget is the GC heap the host hands over,
 * not a frozen-module table */
#define MICROPY_MODULE_BUILTIN_INIT             (1)
#define MICROPY_PY_MICROPYTHON                  (1)
#define MICROPY_PY_SYS                          (1)
#define MICROPY_PY_SYS_EXIT                     (1)
#define MICROPY_PY_SYS_STDFILES                 (0)
#define MICROPY_PY_SYS_PATH_ARGV_DEFAULTS       (1)

/* io stays at the ROM default (on): py/modio.c is always in the embed
 * package's moduledefs (upstream's extractor is textual), so io-off cannot
 * link. The seat has no filesystem — the port faces below (host_upy/
 * port_faces.c, the browser seat's exact !MICROPY_VFS posture) make that
 * honest: lexer-from-file raises ENOENT, import stat reports NO_EXIST and
 * open returns none. Modules arrive from the object loop (mpy bytes in
 * memory), never from a path. */
#define MICROPY_ENABLE_EXTERNAL_IMPORT          (1)

/* GC scanning: the generic gchelper walks registers+stack via setjmp — the
 * embed package ships it (shared/runtime/gchelper_generic.c). */
#define MICROPY_GCREGS_SETJMP                   (1)

/* mp_stack_ctrl: embed_util's mp_embed_init sets the stack top; limit checks
 * stay off (the host runner's own stack discipline governs). */
#define MICROPY_STACK_CHECK                     (0)

/* uncaught exceptions print through mp_plat_print (embed mphalport → stdout);
 * nlr_jump_fail parks instead of crashing the host binary */
#define MICROPY_DEBUG_PRINTERS                  (0)

/* banner/platform strings modsys.c bakes into sys.implementation */
#define MICROPY_PY_SYS_PLATFORM                 "metal-host"
#define MICROPY_BANNER_NAME                      "Metal embedded µPy"

/* mbedtls carrier macros the unix mbedtls_config_port.h reads through
 * extmod/mbedtls/mbedtls_config_common.h (which includes py/mpconfig.h →
 * this config). 0 matches the host binary's TLS shape: no DTLS, platform
 * entropy — the same values the old host_inc stub carried. */
#define MICROPY_PY_SSL_DTLS                     (0)
#define MICROPY_MBEDTLS_CONFIG_BARE_METAL       (0)

#endif /* METAL_INCLUDED_HOST_UPY_MPCONFIGPORT_H */
