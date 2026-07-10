#include <pymergetic/metal/metal.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s <orchestrator.wasm> [vfs_root]\n"
		"  env: WASMTIME  path to wasmtime CLI (default: .tools/wasmtime/bin/wasmtime)\n",
		argv0);
}

int main(int argc, char **argv)
{
	const char *wasm_path;
	const char *vfs_root;
	const char *wasmtime;
	pm_metal_sys_bootstrap_t blob;
	char cmd[2048];

	if (argc < 2) {
		usage(argv[0]);
		return 1;
	}

	wasm_path = argv[1];
	vfs_root = (argc >= 3) ? argv[2] : "build/slice/vfs";
	wasmtime = getenv("WASMTIME");
	if (wasmtime == NULL || wasmtime[0] == '\0') {
		wasmtime = ".tools/wasmtime/bin/wasmtime";
	}

	if (pm_metal_sys_bootstrap_encode(&blob) != 0) {
		fprintf(stderr, "pm_metal_sys_bootstrap_encode failed\n");
		return 1;
	}

	if (pm_metal_sys_hostinfo_publish(vfs_root, &blob, sizeof(blob)) != 0) {
		fprintf(stderr, "pm_metal_sys_hostinfo_publish failed (%s)\n", vfs_root);
		return 1;
	}

	if (snprintf(cmd, sizeof(cmd), "%s run --dir %s::/sys/pm %s",
		     wasmtime, vfs_root, wasm_path) >= (int)sizeof(cmd)) {
		fprintf(stderr, "command too long\n");
		return 1;
	}

	printf("engine: published bootstrap to %s/bootstrap\n", vfs_root);
	fflush(stdout);

	return system(cmd) == 0 ? 0 : 1;
}
