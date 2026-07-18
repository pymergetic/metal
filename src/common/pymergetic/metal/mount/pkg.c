/*
 * Named guest packages — impl: common. See pkg.h.
 */
#include "pymergetic/metal/mount/pkg.h"

#include <stdio.h>
#include <string.h>

#include "pymergetic/metal/mount/populate.h"

typedef struct pm_metal_pkg_entry {
	int used;
	int applied;
	char id[PM_METAL_MOUNT_PKG_ID_MAX];
	const uint8_t *blob;
	size_t blob_len;
	size_t uncompressed_len;
	unsigned flags;
	const char *deps[PM_METAL_MOUNT_PKG_DEPS_MAX];
	size_t ndeps;
} pm_metal_pkg_entry_t;

static pm_metal_pkg_entry_t g_pm_metal_pkg_table[PM_METAL_MOUNT_PKG_MAX];

static pm_metal_pkg_entry_t *pm_metal_pkg_find(const char *id)
{
	int i;

	if (!id || !id[0]) {
		return NULL;
	}
	for (i = 0; i < PM_METAL_MOUNT_PKG_MAX; i++) {
		if (g_pm_metal_pkg_table[i].used && strcmp(g_pm_metal_pkg_table[i].id, id) == 0) {
			return &g_pm_metal_pkg_table[i];
		}
	}
	return NULL;
}

int pm_metal_pkg_register(const char *id, const uint8_t *blob, size_t blob_len,
			   size_t uncompressed_len, unsigned flags, const char *const *deps,
			   size_t ndeps)
{
	pm_metal_pkg_entry_t *e;
	int i;
	size_t d;

	if (!id || !id[0] || strlen(id) >= PM_METAL_MOUNT_PKG_ID_MAX || !blob || blob_len == 0) {
		return -1;
	}
	if ((flags & PM_METAL_MOUNT_PKG_FLAG_LZ4) && uncompressed_len == 0) {
		return -1;
	}
	if (ndeps > PM_METAL_MOUNT_PKG_DEPS_MAX || (ndeps > 0 && !deps)) {
		return -1;
	}
	if (pm_metal_pkg_find(id)) {
		fprintf(stderr, "pm_metal_pkg: duplicate id '%s'\n", id);
		return -1;
	}
	e = NULL;
	for (i = 0; i < PM_METAL_MOUNT_PKG_MAX; i++) {
		if (!g_pm_metal_pkg_table[i].used) {
			e = &g_pm_metal_pkg_table[i];
			break;
		}
	}
	if (!e) {
		fprintf(stderr, "pm_metal_pkg: registry full\n");
		return -1;
	}
	memset(e, 0, sizeof(*e));
	e->used = 1;
	snprintf(e->id, sizeof(e->id), "%s", id);
	e->blob = blob;
	e->blob_len = blob_len;
	e->uncompressed_len = uncompressed_len;
	e->flags = flags;
	e->ndeps = ndeps;
	for (d = 0; d < ndeps; d++) {
		if (!deps[d] || !deps[d][0]) {
			e->used = 0;
			return -1;
		}
		e->deps[d] = deps[d];
	}
	return 0;
}

void pm_metal_pkg_clear(void)
{
	memset(g_pm_metal_pkg_table, 0, sizeof(g_pm_metal_pkg_table));
}

/* Strong definition in build/guest-pkgs/pkgs_init.c (always composed).
 * Must not be a weak stub here — a local weak definition prevents the
 * linker from pulling pkgs_init.o out of NuttX static app archives. */

/* visit: 0 = white, 1 = grey, 2 = black */
static int pm_metal_pkg_dfs(pm_metal_pkg_entry_t *e, int *visit, pm_metal_pkg_entry_t **order,
			     size_t *norder)
{
	size_t d;
	int idx = (int)(e - g_pm_metal_pkg_table);

	if (visit[idx] == 1) {
		fprintf(stderr, "pm_metal_pkg: dependency cycle at '%s'\n", e->id);
		return -1;
	}
	if (visit[idx] == 2) {
		return 0;
	}
	visit[idx] = 1;
	for (d = 0; d < e->ndeps; d++) {
		pm_metal_pkg_entry_t *dep = pm_metal_pkg_find(e->deps[d]);

		if (!dep) {
			fprintf(stderr, "pm_metal_pkg: '%s' missing dep '%s'\n", e->id, e->deps[d]);
			return -1;
		}
		if (pm_metal_pkg_dfs(dep, visit, order, norder) != 0) {
			return -1;
		}
	}
	visit[idx] = 2;
	order[(*norder)++] = e;
	return 0;
}

static int pm_metal_pkg_apply_entry(pm_metal_pkg_entry_t *e)
{
	unsigned pop_flags;

	if (e->applied) {
		return 0;
	}
	pop_flags = (e->flags & PM_METAL_MOUNT_PKG_FLAG_LZ4) ? PM_METAL_MOUNT_POPULATE_FLAG_LZ4 : 0u;
	if (pm_metal_mount_populate_extract(e->blob, e->blob_len, e->uncompressed_len, pop_flags) != 0) {
		fprintf(stderr, "pm_metal_pkg: extract failed for '%s'\n", e->id);
		return -1;
	}
	e->applied = 1;
	return 0;
}

int pm_metal_pkg_ensure(const char *id)
{
	pm_metal_pkg_entry_t *e;
	int visit[PM_METAL_MOUNT_PKG_MAX];
	pm_metal_pkg_entry_t *order[PM_METAL_MOUNT_PKG_MAX];
	size_t norder = 0;
	size_t i;

	pm_metal_pkg_embed_init();
	e = pm_metal_pkg_find(id);
	if (!e) {
		fprintf(stderr, "pm_metal_pkg: unknown id '%s'\n", id ? id : "(null)");
		return -1;
	}
	memset(visit, 0, sizeof(visit));
	if (pm_metal_pkg_dfs(e, visit, order, &norder) != 0) {
		return -1;
	}
	for (i = 0; i < norder; i++) {
		if (pm_metal_pkg_apply_entry(order[i]) != 0) {
			return -1;
		}
	}
	return 0;
}

int pm_metal_pkg_apply_all(void)
{
	int visit[PM_METAL_MOUNT_PKG_MAX];
	pm_metal_pkg_entry_t *order[PM_METAL_MOUNT_PKG_MAX];
	size_t norder = 0;
	int i;
	size_t k;

	pm_metal_pkg_embed_init();
	memset(visit, 0, sizeof(visit));
	for (i = 0; i < PM_METAL_MOUNT_PKG_MAX; i++) {
		if (!g_pm_metal_pkg_table[i].used) {
			continue;
		}
		if (pm_metal_pkg_dfs(&g_pm_metal_pkg_table[i], visit, order, &norder) != 0) {
			return -1;
		}
	}
	for (k = 0; k < norder; k++) {
		if (pm_metal_pkg_apply_entry(order[k]) != 0) {
			return -1;
		}
	}
	return 0;
}
