/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * API visibility — orchestrator/host builds only.
 *
 * Set PM_MAX_VIS per build personality:
 *   orchestrator / host   PM_MAX_VIS=1   (PM_VIS_RUNTIME, default)
 *   debug build           PM_MAX_VIS=2   (PM_VIS_DEBUG)
 *
 * Wasm mods do NOT use this header. They compile with include/pymergetic/mod/
 * only (separate include path — no PM_VIS_MOD macro gating).
 *
 * Transitional native .o mods: -DPM_MAX_VIS=0 (PM_VIS_MOD) + export/pm_export_v1.h
 * until wasm mods replace freestanding objects.
 */

#ifndef PM_VIS_H_
#define PM_VIS_H_

#define PM_VIS_MOD 0
#define PM_VIS_RUNTIME 1
#define PM_VIS_DEBUG 2

#ifndef PM_MAX_VIS
#define PM_MAX_VIS PM_VIS_RUNTIME
#endif

typedef enum pm_vis {
	PM_VIS_MOD_LVL = PM_VIS_MOD,
	PM_VIS_RUNTIME_LVL = PM_VIS_RUNTIME,
	PM_VIS_DEBUG_LVL = PM_VIS_DEBUG,
} pm_vis_t;

#define PM_VIS_CAT(a, b) PM_VIS_CAT_I(a, b)
#define PM_VIS_CAT_I(a, b) a##b

#define PM_API(level, ret, name, args) PM_VIS_CAT(PM_API_LEVEL_, level)(ret, name, args)

#if PM_MAX_VIS >= PM_VIS_MOD
#define PM_API_LEVEL_0(ret, name, args) ret name args;
#else
#define PM_API_LEVEL_0(ret, name, args)
#endif

#if PM_MAX_VIS >= PM_VIS_RUNTIME
#define PM_API_LEVEL_1(ret, name, args) ret name args;
#else
#define PM_API_LEVEL_1(ret, name, args)
#endif

#if PM_MAX_VIS >= PM_VIS_DEBUG
#define PM_API_LEVEL_2(ret, name, args) ret name args;
#else
#define PM_API_LEVEL_2(ret, name, args)
#endif

#define PM_DECL(level, decl) PM_VIS_CAT(PM_DECL_LEVEL_, level)(decl)

#if PM_MAX_VIS >= PM_VIS_MOD
#define PM_DECL_LEVEL_0(decl) decl;
#else
#define PM_DECL_LEVEL_0(decl)
#endif

#if PM_MAX_VIS >= PM_VIS_RUNTIME
#define PM_DECL_LEVEL_1(decl) decl;
#else
#define PM_DECL_LEVEL_1(decl)
#endif

#if PM_MAX_VIS >= PM_VIS_DEBUG
#define PM_DECL_LEVEL_2(decl) decl;
#else
#define PM_DECL_LEVEL_2(decl)
#endif

#endif /* PM_VIS_H_ */
