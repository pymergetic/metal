/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal mod smoke — compile with scripts/build-mod.sh (PM_MAX_VIS=PM_VIS_MOD).
 */

#include <pymergetic/export/pm_export_v1.h>

void pm_mod_entry(void)
{
	if (pm_export != NULL && pm_export->log != NULL) {
		pm_export->log("hello from mod");
	}
}
