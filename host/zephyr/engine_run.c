/*
 * Zephyr engine entry — publishes bootstrap; WAMR orchestrator load comes next.
 * Twin slice today: build as linux-hosted binary via scripts/build-engine-zephyr.sh
 */
#include <pymergetic/metal/metal.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
	const char *wasm_path;
	const char *vfs_root;
	const char *wasmtime;
	pm_metal_sys_bootstrap_t blob;
	char cmd[2048];

	if (argc < 2) {
		fprintf(stderr, "usage: %s <orchestrator.wasm> [vfs_root]\n", argv[0]);
		return 1;
	}

	wasm_path = argv[1];
	vfs_root = (argc >= 3) ? argv[2] : "build/slice/vfs-zephyr";
	wasmtime = getenv("WASMTIME");
	if (wasmtime == NULL || wasmtime[0] == '\0') {
		wasmtime = ".tools/wasmtime/bin/wasmtime";
	}

	if (pm_metal_sys_bootstrap_encode(&blob) != 0) {
		return 1;
	}

	if (pm_metal_sys_hostinfo_publish(vfs_root, &blob, sizeof(blob)) != 0) {
		return 1;
	}

	printf("engine: target=zephyr machine_ram=%llu arena_budget=%llu\n",
	       (unsigned long long)blob.machine_ram, (unsigned long long)blob.arena_budget);
	printf("engine: published bootstrap to %s/bootstrap\n", vfs_root);
	fflush(stdout);

	if (snprintf(cmd, sizeof(cmd), "%s run --dir %s::/sys/pm %s",
		     wasmtime, vfs_root, wasm_path) >= (int)sizeof(cmd)) {
		return 1;
	}

	return system(cmd) == 0 ? 0 : 1;
}
