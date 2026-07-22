/*
 * Shared attribute shape for Metal wasi-style imports (util/, net/, mount/).
 * Each header still picks its own import_module name string; this macro
 * only unifies the __attribute__ form so guest declarations and that
 * module's host wasm_runtime_register_natives() cannot drift apart.
 *
 * impl: none — header-only (PM_METAL_WASI_IMPORT macro)
 */
#ifndef PYMERGETIC_METAL_WASI_H_
#define PYMERGETIC_METAL_WASI_H_

#define PM_METAL_WASI_IMPORT(module, name) \
	__attribute__((import_module(module), import_name(#name)))

#endif /* PYMERGETIC_METAL_WASI_H_ */
