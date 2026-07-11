/*
 * SPDX-License-Identifier: Apache-2.0
 * Zephyr runtime entry — delegates to shared host/common runtime boot.
 */
#include <pymergetic/metal/runtime/entry.h>
#include <pymergetic/metal/sys/sys.h>

#include <zephyr/autoconf.h>
#include <zephyr/sys/util.h>

#if IS_ENABLED(CONFIG_PM_METAL_VERIFY_MOD_SMOKE)
#include "verify_mod_smoke.h"
#endif

int main(void)
{
	pm_metal_runtime_config_t cfg = {
		.target = "zephyr",
		.handoff_vfs_root = PM_METAL_SYS_HANDOFF_VFS_ROOT,
	};
	int rc;

	rc = pm_metal_runtime_main(&cfg);
	if (rc != 0) {
		return 1;
	}

#if IS_ENABLED(CONFIG_PM_METAL_VERIFY_MOD_SMOKE)
	if (pm_metal_verify_mod_smoke_run() != 0) {
		return 1;
	}
#endif

	return 0;
}
