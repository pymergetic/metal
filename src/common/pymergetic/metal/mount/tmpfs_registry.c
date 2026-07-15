/*
 * tmpfs name registry — impl: common. See tmpfs_registry.h.
 */
#include "pymergetic/metal/mount/tmpfs_registry.h"

#include <string.h>

#include "pymergetic/metal/mount/table.h"

/* One entry per distinct name ever establish()'d this boot — bounded by
 * PM_METAL_MOUNT_MAX for the same reason mount.c's own table is: at most
 * that many mount-table entries can exist at once, so at most that many
 * distinct tmpfs names can be in play (worst case, one name per entry). */
#define PM_METAL_MOUNT_TMPFS_NAME_MAX 64

typedef struct pm_metal_mount_tmpfs_entry {
	int used;
	char name[PM_METAL_MOUNT_TMPFS_NAME_MAX];
	char host_path[PM_METAL_MOUNT_HOST_PATH_MAX];
	int refcount;
} pm_metal_mount_tmpfs_entry_t;

static pm_metal_mount_tmpfs_entry_t g_pm_metal_mount_tmpfs_registry[PM_METAL_MOUNT_MAX];

static pm_metal_mount_tmpfs_entry_t *pm_metal_mount_tmpfs_find_by_name(const char *name)
{
	int i;

	for (i = 0; i < PM_METAL_MOUNT_MAX; i++) {
		if (g_pm_metal_mount_tmpfs_registry[i].used
		    && strcmp(g_pm_metal_mount_tmpfs_registry[i].name, name) == 0) {
			return &g_pm_metal_mount_tmpfs_registry[i];
		}
	}
	return NULL;
}

static pm_metal_mount_tmpfs_entry_t *pm_metal_mount_tmpfs_find_by_host_path(const char *host_path)
{
	int i;

	for (i = 0; i < PM_METAL_MOUNT_MAX; i++) {
		if (g_pm_metal_mount_tmpfs_registry[i].used
		    && strcmp(g_pm_metal_mount_tmpfs_registry[i].host_path, host_path) == 0) {
			return &g_pm_metal_mount_tmpfs_registry[i];
		}
	}
	return NULL;
}

int pm_metal_mount_tmpfs_registry_acquire(const char *name, char *out_host_path, size_t out_cap)
{
	pm_metal_mount_tmpfs_entry_t *e;
	size_t len;

	if (!name || !name[0] || !out_host_path) {
		return -1;
	}
	e = pm_metal_mount_tmpfs_find_by_name(name);
	if (!e) {
		return -1;
	}
	len = strlen(e->host_path);
	if (len + 1 > out_cap) {
		return -1;
	}
	memcpy(out_host_path, e->host_path, len + 1);
	e->refcount++;
	return 0;
}

int pm_metal_mount_tmpfs_registry_insert(const char *name, const char *host_path)
{
	pm_metal_mount_tmpfs_entry_t *slot = NULL;
	size_t name_len;
	size_t host_len;
	int i;

	if (!name || !name[0] || !host_path || !host_path[0]) {
		return -1;
	}
	name_len = strlen(name);
	host_len = strlen(host_path);
	if (name_len + 1 > sizeof(slot->name) || host_len + 1 > sizeof(slot->host_path)) {
		return -1;
	}
	for (i = 0; i < PM_METAL_MOUNT_MAX; i++) {
		if (!g_pm_metal_mount_tmpfs_registry[i].used) {
			slot = &g_pm_metal_mount_tmpfs_registry[i];
			break;
		}
	}
	if (!slot) {
		return -1;
	}
	memcpy(slot->name, name, name_len + 1);
	memcpy(slot->host_path, host_path, host_len + 1);
	slot->refcount = 1;
	slot->used = 1;
	return 0;
}

int pm_metal_mount_tmpfs_registry_release(const char *host_path)
{
	pm_metal_mount_tmpfs_entry_t *e;

	if (!host_path) {
		return -1;
	}
	e = pm_metal_mount_tmpfs_find_by_host_path(host_path);
	if (!e) {
		return -1;
	}
	e->refcount--;
	if (e->refcount > 0) {
		return 0;
	}
	memset(e, 0, sizeof(*e));
	return 1;
}
