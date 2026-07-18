/*
 * T8 — multi-module demo, library half. Exports add() for another .wasm
 * to import directly (see mods/tests/t9_multimod_app) — proving WAMR's own
 * WASM_ENABLE_MULTI_MODULE feature end to end: no host round-trip for
 * this one call at all, unlike every native import elsewhere in this tree
 * (util/{arena,log,size}.h) which all cross into host C code.
 *
 * Built as a wasi "reactor" (this directory's own empty REACTOR marker
 * file — see scripts/build mod none), i.e. no _start/main of its own: WAMR's
 * own multi-module loader flatly rejects a "command" module (one with a
 * _start) as a sub-module ("a command (with _start function) can not be a
 * sub-module", from wasm_loader.c) — found by trying the command-module
 * shape first and hitting exactly that error. runtime/runtime.c's own
 * module_reader loads this file under the module name "t8_multimod_lib"
 * (matching this directory's own name — see that function's own doc
 * comment) the moment mods/tests/t9_multimod_app's own import section first
 * names it; nothing here has to know that's how it's found. Not run
 * standalone by scripts/verify linux's own driver list for exactly that
 * reason — a reactor has no entry point that run()'s own
 * wasm_application_execute_main() could call.
 */
#include <stdint.h>

__attribute__((export_name("add"))) int32_t
t8_multimod_lib_add(int32_t a, int32_t b)
{
	return a + b;
}
