#include <pymergetic/metal/runtime/entry.h>

#include <pymergetic/metal/orchestrator/boot.h>
#include <pymergetic/metal/sys/hostinfo.h>
#include <pymergetic/metal/sys/sys.h>

#include <stdio.h>

int pm_metal_runtime_main(const pm_metal_runtime_config_t *cfg)
{
	pm_metal_sys_bootstrap_t blob;
	int rc;

	if (cfg == NULL || cfg->target == NULL || cfg->handoff_vfs_root == NULL ||
	    cfg->handoff_vfs_root[0] == '\0') {
		return 1;
	}

	if (pm_metal_sys_bootstrap_encode(&blob) != 0) {
		fprintf(stderr, "pm_metal_sys_bootstrap_encode failed\n");
		return 1;
	}

	printf("runtime: target=%s machine_ram=%llu arena_budget=%llu\n", cfg->target,
	       (unsigned long long)blob.machine_ram, (unsigned long long)blob.arena_budget);
	fflush(stdout);

	if (pm_metal_sys_hostinfo_publish(cfg->handoff_vfs_root, &blob, sizeof(blob)) != 0) {
		fprintf(stderr, "pm_metal_sys_hostinfo_publish failed (%s)\n", cfg->handoff_vfs_root);
		return 1;
	}

	printf("runtime: published bootstrap to %s/bootstrap\n", cfg->handoff_vfs_root);
	fflush(stdout);

	rc = pm_metal_orchestrator_boot(&blob);
	if (rc != 0) {
		fprintf(stderr, "orchestrator boot failed (%d)\n", rc);
		return 1;
	}

	return 0;
}
