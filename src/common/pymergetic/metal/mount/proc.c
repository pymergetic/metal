/*
 * procfs — registry + virtual `proc` fstype (no host dir). See proc.h.
 */
#include "pymergetic/metal/mount/proc.h"

#include <stdio.h>
#include <string.h>

#include "pymergetic/metal/mount/proc/cpuinfo.h"
#include "pymergetic/metal/mount/proc/filesystems.h"
#include "pymergetic/metal/mount/proc/meminfo.h"
#include "pymergetic/metal/mount/proc/mounts.h"
#include "pymergetic/metal/mount/proc/uptime.h"
#include "pymergetic/metal/mount/proc/version.h"

typedef struct pm_metal_mount_proc_hook {
	int used;
	char name[PM_METAL_MOUNT_PROC_NAME_MAX];
	pm_metal_mount_proc_hook_fn fn;
} pm_metal_mount_proc_hook_t;

static pm_metal_mount_proc_hook_t g_pm_metal_mount_proc_hooks[PM_METAL_MOUNT_PROC_MAX_HOOKS];
static int g_pm_metal_mount_proc_builtins;

static void pm_metal_mount_proc_ensure_builtins(void)
{
	if (g_pm_metal_mount_proc_builtins) {
		return;
	}
	/* cmdline/environ are only via /proc/self/ — see WASI proc layer. */
	(void)pm_metal_mount_proc_register("mounts", pm_metal_mount_proc_generate_mounts);
	(void)pm_metal_mount_proc_register("filesystems", pm_metal_mount_proc_generate_filesystems);
	(void)pm_metal_mount_proc_register("version", pm_metal_mount_proc_generate_version);
	(void)pm_metal_mount_proc_register("cpuinfo", pm_metal_mount_proc_generate_cpuinfo);
	(void)pm_metal_mount_proc_register("meminfo", pm_metal_mount_proc_generate_meminfo);
	(void)pm_metal_mount_proc_register("uptime", pm_metal_mount_proc_generate_uptime);
	g_pm_metal_mount_proc_builtins = 1;
}

int pm_metal_mount_proc_is_sentinel(const char *host_path)
{
	return host_path && strcmp(host_path, PM_METAL_MOUNT_PROC_SENTINEL) == 0;
}

int pm_metal_mount_proc_register(const char *name, pm_metal_mount_proc_hook_fn fn)
{
	int i;
	int free_slot = -1;

	if (!name || !name[0] || name[0] == '/' || !fn) {
		return -1;
	}
	if (strlen(name) >= PM_METAL_MOUNT_PROC_NAME_MAX) {
		return -1;
	}

	for (i = 0; i < PM_METAL_MOUNT_PROC_MAX_HOOKS; i++) {
		if (g_pm_metal_mount_proc_hooks[i].used
		    && strcmp(g_pm_metal_mount_proc_hooks[i].name, name) == 0) {
			g_pm_metal_mount_proc_hooks[i].fn = fn;
			return 0;
		}
		if (!g_pm_metal_mount_proc_hooks[i].used && free_slot < 0) {
			free_slot = i;
		}
	}
	if (free_slot < 0) {
		return -1;
	}
	snprintf(g_pm_metal_mount_proc_hooks[free_slot].name, sizeof(g_pm_metal_mount_proc_hooks[free_slot].name),
		 "%s", name);
	g_pm_metal_mount_proc_hooks[free_slot].fn = fn;
	g_pm_metal_mount_proc_hooks[free_slot].used = 1;
	return 0;
}

int pm_metal_mount_proc_lookup(const char *name, pm_metal_mount_proc_hook_fn *out_fn)
{
	int i;

	if (!name || !out_fn) {
		return -1;
	}
	pm_metal_mount_proc_ensure_builtins();
	for (i = 0; i < PM_METAL_MOUNT_PROC_MAX_HOOKS; i++) {
		if (g_pm_metal_mount_proc_hooks[i].used
		    && strcmp(g_pm_metal_mount_proc_hooks[i].name, name) == 0) {
			*out_fn = g_pm_metal_mount_proc_hooks[i].fn;
			return 0;
		}
	}
	return -1;
}

int pm_metal_mount_proc_hook_count(void)
{
	int i;
	int n = 0;

	pm_metal_mount_proc_ensure_builtins();
	for (i = 0; i < PM_METAL_MOUNT_PROC_MAX_HOOKS; i++) {
		if (g_pm_metal_mount_proc_hooks[i].used) {
			n++;
		}
	}
	return n;
}

const char *pm_metal_mount_proc_hook_name(int index)
{
	int i;
	int n = 0;

	if (index < 0) {
		return NULL;
	}
	pm_metal_mount_proc_ensure_builtins();
	for (i = 0; i < PM_METAL_MOUNT_PROC_MAX_HOOKS; i++) {
		if (!g_pm_metal_mount_proc_hooks[i].used) {
			continue;
		}
		if (n == index) {
			return g_pm_metal_mount_proc_hooks[i].name;
		}
		n++;
	}
	return NULL;
}

static int pm_metal_mount_proc_establish(const char *source, const char *opts, char *out_host_path,
					  size_t out_cap)
{
	size_t len = strlen(PM_METAL_MOUNT_PROC_SENTINEL);

	(void)source;
	(void)opts;

	if (!out_host_path || out_cap <= len) {
		return -1;
	}
	memcpy(out_host_path, PM_METAL_MOUNT_PROC_SENTINEL, len + 1);
	pm_metal_mount_proc_ensure_builtins();
	return 0;
}

static void pm_metal_mount_proc_release(const char *host_path)
{
	(void)host_path;
}

static const pm_metal_mount_ops_t g_pm_metal_mount_proc_ops = {
	.establish = pm_metal_mount_proc_establish,
	.release = pm_metal_mount_proc_release,
};

const pm_metal_mount_ops_t *pm_metal_mount_proc_ops(void)
{
	return &g_pm_metal_mount_proc_ops;
}
