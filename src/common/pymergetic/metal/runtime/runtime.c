/*
 * Runtime — common implementations (WAMR dynamic loader).
 */
#include "pymergetic/metal/runtime/runtime.h"

int pm_metal_runtime_init(const pm_metal_runtime_config_t *cfg)
{
	(void)cfg;
	return -1;
}

int pm_metal_runtime_shutdown(void)
{
	return -1;
}

int pm_metal_runtime_load_file(const char *path, pm_metal_runtime_handle_t *out)
{
	(void)path;
	(void)out;
	return -1;
}

int pm_metal_runtime_load_bytes(const uint8_t *wasm, uint32_t len,
			      pm_metal_runtime_handle_t *out)
{
	(void)wasm;
	(void)len;
	(void)out;
	return -1;
}

int pm_metal_runtime_run(pm_metal_runtime_handle_t h, int argc, char **argv)
{
	(void)h;
	(void)argc;
	(void)argv;
	return -1;
}

int pm_metal_runtime_unload(pm_metal_runtime_handle_t h)
{
	(void)h;
	return -1;
}
