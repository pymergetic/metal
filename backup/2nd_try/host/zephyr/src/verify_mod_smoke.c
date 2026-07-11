/*
 * SPDX-License-Identifier: Apache-2.0
 * Verify — run embedded mod-smoke.wasm via WAMR.
 */
#include "verify_mod_smoke.h"

#include <pymergetic/metal/orchestrator/wasm_host.h>

#include <zephyr/sys/printk.h>

extern const unsigned char pm_mod_smoke_wasm[];
extern const unsigned int pm_mod_smoke_wasm_len;

int pm_metal_verify_mod_smoke_run(void)
{
	printk("verify_mod: running embedded mod-smoke (%u bytes)\n", pm_mod_smoke_wasm_len);
	return pm_metal_orchestrator_wasm_host_run_bytes(pm_mod_smoke_wasm, pm_mod_smoke_wasm_len);
}
