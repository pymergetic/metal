/*
 * T9 — multi-module demo, app half. Imports add() straight from
 * mods/t8_multimod_lib's own .wasm (see that mod's own header) instead of
 * from a host module. Proves runtime/runtime.c's own module_reader
 * actually gets invoked and wired correctly on this end too: this
 * module's import section names "t8_multimod_lib" as a *module*, not a
 * file, and WAMR only resolves that name to bytes because module_reader
 * turns it into /mods/t8_multimod_lib.wasm on this same vfs_root, lazily,
 * the moment this module is load_file()'d — this mod never opens that
 * path itself.
 */
#include <stdint.h>
#include <stdio.h>

__attribute__((import_module("t8_multimod_lib"), import_name("add"))) int32_t
t8_multimod_lib_add(int32_t a, int32_t b);

int main(void)
{
	int32_t result = t8_multimod_lib_add(3, 4);

	printf("t9_multimod_app: t8_multimod_lib_add(3, 4) = %d\n", result);
	return 0;
}
