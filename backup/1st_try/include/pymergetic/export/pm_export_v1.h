/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mod import surface — host loader binds pm_export before calling mod entry points.
 */

#ifndef PM_EXPORT_V1_H_
#define PM_EXPORT_V1_H_

#include <pymergetic/pm_vis.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if PM_MAX_VIS >= PM_VIS_MOD

#define PM_EXPORT_V1 1

typedef struct pm_export_v1 {
	void (*log)(const char *msg);
	void *(*alloc)(size_t size);
	void (*free)(void *ptr);
} pm_export_v1_t;

PM_DECL(PM_VIS_MOD, extern const pm_export_v1_t *pm_export)

#endif /* PM_MAX_VIS >= PM_VIS_MOD */

#ifdef __cplusplus
}
#endif

#endif /* PM_EXPORT_V1_H_ */
