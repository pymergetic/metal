/*
 * Shared shape for every util header's wasi-style import (see arena.h,
 * log.h, size.h) — one macro so all three read identically, but each header
 * still picks its *own* `import_module` name (its own
 * PM_METAL_UTIL_<MODULE>_WASI_MODULE, defined in that header, right next
 * to the `_IMPORT()` macro built from it below) rather than sharing one
 * name across the whole package: dots are ordinary bytes to wasm's
 * import section (no special parsing, just an opaque string compared
 * with strcmp on the host side), so "pymergetic.metal.util.arena" /
 * ".log" / ".size" costs nothing and namespaces each module on its own —
 * this is not "env", the Emscripten-popularized default module name for
 * host glue; real per-module names here mean none of these imports can
 * ever collide with anything else, including each other.
 *
 * The point of unifying just this macro (not the module-name strings
 * themselves) is that the guest-side `import_module(...)` attribute and
 * that same module's own host-side wasm_runtime_register_natives() call
 * (arena.c/log.c/size.c) both build from the exact same
 * PM_METAL_UTIL_<MODULE>_WASI_MODULE constant, so they can never drift
 * apart into two different strings.
 */
#ifndef PYMERGETIC_METAL_UTIL_WASI_H_
#define PYMERGETIC_METAL_UTIL_WASI_H_

#define PM_METAL_UTIL_WASI_IMPORT(module, name) \
	__attribute__((import_module(module), import_name(#name)))

#endif /* PYMERGETIC_METAL_UTIL_WASI_H_ */
