/*
 * tmpfs name registry — impl: common. See tmpfs_registry.h.
 */
#include "pymergetic/metal/mount/tmpfs_registry.h"

#include <string.h>

#include "pymergetic/metal/mount/table.h"
#include "pymergetic/metal/port/lock.h"

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
static pm_metal_port_mutex_t g_pm_metal_mount_tmpfs_registry_lock;
static pm_metal_port_once_t g_pm_metal_mount_tmpfs_registry_lock_once = PM_METAL_PORT_ONCE_INIT;

static void pm_metal_mount_tmpfs_registry_lock(void)
{
	pm_metal_port_mutex_ensure(&g_pm_metal_mount_tmpfs_registry_lock,
				    &g_pm_metal_mount_tmpfs_registry_lock_once);
	pm_metal_port_mutex_lock(&g_pm_metal_mount_tmpfs_registry_lock);
}

static void pm_metal_mount_tmpfs_registry_unlock(void)
{
	pm_metal_port_mutex_unlock(&g_pm_metal_mount_tmpfs_registry_lock);
}

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
	int rc = -1;

	if (!name || !name[0] || !out_host_path) {
		return -1;
	}
	pm_metal_mount_tmpfs_registry_lock();
	e = pm_metal_mount_tmpfs_find_by_name(name);
	if (e) {
		len = strlen(e->host_path);
		if (len + 1 <= out_cap) {
			memcpy(out_host_path, e->host_path, len + 1);
			e->refcount++;
			rc = 0;
		}
	}
	pm_metal_mount_tmpfs_registry_unlock();
	return rc;
}

int pm_metal_mount_tmpfs_registry_insert(const char *name, const char *host_path)
{
	pm_metal_mount_tmpfs_entry_t *slot = NULL;
	size_t name_len;
	size_t host_len;
	int i;
	int rc = -1;

	if (!name || !name[0] || !host_path || !host_path[0]) {
		return -1;
	}
	name_len = strlen(name);
	host_len = strlen(host_path);
	if (name_len + 1 > PM_METAL_MOUNT_TMPFS_NAME_MAX || host_len + 1 > PM_METAL_MOUNT_HOST_PATH_MAX) {
		return -1;
	}
	pm_metal_mount_tmpfs_registry_lock();
	for (i = 0; i < PM_METAL_MOUNT_MAX; i++) {
		if (!g_pm_metal_mount_tmpfs_registry[i].used) {
			slot = &g_pm_metal_mount_tmpfs_registry[i];
			break;
		}
	}
	if (slot) {
		memcpy(slot->name, name, name_len + 1);
		memcpy(slot->host_path, host_path, host_len + 1);
		slot->refcount = 1;
		slot->used = 1;
		rc = 0;
	}
	pm_metal_mount_tmpfs_registry_unlock();
	return rc;
}

int pm_metal_mount_tmpfs_registry_release(const char *host_path)
{
	pm_metal_mount_tmpfs_entry_t *e;
	int rc = -1;

	if (!host_path) {
		return -1;
	}
	pm_metal_mount_tmpfs_registry_lock();
	e = pm_metal_mount_tmpfs_find_by_host_path(host_path);
	if (e) {
		e->refcount--;
		if (e->refcount > 0) {
			rc = 0;
		} else {
			memset(e, 0, sizeof(*e));
			rc = 1;
		}
	}
	pm_metal_mount_tmpfs_registry_unlock();
	return rc;
}
