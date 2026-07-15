/*
 * Mount table — impl: common. See table.h.
 */
#include "pymergetic/metal/mount/table.h"

#include <stdio.h>
#include <string.h>

typedef struct pm_metal_mount_entry {
	int used;
	char guest_path[PM_METAL_MOUNT_GUEST_PATH_MAX]; /* normalized — see normalize() below */
	pm_metal_mount_kind_t kind;
	char host_path[PM_METAL_MOUNT_HOST_PATH_MAX]; /* whatever this kind's own establish() wrote */
	char source[PM_METAL_MOUNT_HOST_PATH_MAX]; /* fstab/CLI source column, for /proc/mounts */
	char opts[PM_METAL_MOUNT_OPTS_MAX]; /* raw options string (may be empty) */
	int readonly; /* tracked, not yet enforced — see table.h */
} pm_metal_mount_entry_t;

static pm_metal_mount_entry_t g_pm_metal_mount_table[PM_METAL_MOUNT_MAX];

/* "/foo" stays "/foo"; "foo" becomes "/foo"; "/foo/" becomes "/foo"; "/"
 * stays "/" (the one path this never strips the trailing slash from —
 * there is nothing left to strip). Same normalization wasi-libc itself
 * expects of a map_dir_list guest-side prefix. */
static int pm_metal_mount_normalize(const char *in, char *out, size_t out_cap)
{
	int n;
	size_t len;

	if (!in || !in[0] || !out) {
		return -1;
	}
	n = snprintf(out, out_cap, "%s%s", in[0] == '/' ? "" : "/", in);
	if (n <= 0 || (size_t)n >= out_cap) {
		return -1;
	}
	len = (size_t)n;
	if (len > 1 && out[len - 1] == '/') {
		out[len - 1] = '\0';
	}
	return 0;
}

static pm_metal_mount_entry_t *pm_metal_mount_find_exact(const char *norm_guest_path)
{
	int i;

	for (i = 0; i < PM_METAL_MOUNT_MAX; i++) {
		if (g_pm_metal_mount_table[i].used
		    && strcmp(g_pm_metal_mount_table[i].guest_path, norm_guest_path) == 0) {
			return &g_pm_metal_mount_table[i];
		}
	}
	return NULL;
}

/* Longest-prefix match — see table.h's own doc comment on resolve(). A
 * prefix only counts if the next char in norm_guest_path is '/' or the
 * string end, so "/data" never matches a query like "/database" — same
 * boundary rule wasi-libc's own libc-find-relpath.c uses. */
static const pm_metal_mount_entry_t *pm_metal_mount_find_best(const char *norm_guest_path)
{
	const pm_metal_mount_entry_t *best = NULL;
	size_t best_len = 0;
	int i;

	for (i = 0; i < PM_METAL_MOUNT_MAX; i++) {
		const pm_metal_mount_entry_t *e = &g_pm_metal_mount_table[i];
		size_t plen;

		if (!e->used) {
			continue;
		}
		plen = strlen(e->guest_path);
		if (plen == 1) {
			/* root "/" matches everything, at the lowest priority. */
			if (best_len < 1) {
				best = e;
				best_len = 1;
			}
			continue;
		}
		if (strncmp(norm_guest_path, e->guest_path, plen) == 0
		    && (norm_guest_path[plen] == '/' || norm_guest_path[plen] == '\0')) {
			if (plen > best_len) {
				best = e;
				best_len = plen;
			}
		}
	}
	return best;
}

/* Plain comma-separated token scan for the two generic options — no
 * quoting, no key=value here (that's kind-specific, left in the full
 * opts string handed to establish() untouched). */
static int pm_metal_mount_opts_has(const char *opts, const char *token)
{
	const char *p = opts;
	size_t tok_len = strlen(token);

	if (!opts) {
		return 0;
	}
	while (*p) {
		const char *start = p;

		while (*p && *p != ',') {
			p++;
		}
		if ((size_t)(p - start) == tok_len && strncmp(start, token, tok_len) == 0) {
			return 1;
		}
		if (*p == ',') {
			p++;
		}
	}
	return 0;
}

static pm_metal_mount_entry_t *pm_metal_mount_find_free_slot(void)
{
	int i;

	for (i = 0; i < PM_METAL_MOUNT_MAX; i++) {
		if (!g_pm_metal_mount_table[i].used) {
			return &g_pm_metal_mount_table[i];
		}
	}
	return NULL;
}

int pm_metal_mount(const char *guest_path, pm_metal_mount_kind_t kind, const char *source, const char *opts)
{
	char norm[PM_METAL_MOUNT_GUEST_PATH_MAX];
	const pm_metal_mount_ops_t *ops;
	pm_metal_mount_entry_t *slot;
	char host_path[PM_METAL_MOUNT_HOST_PATH_MAX];
	size_t host_len;

	if (pm_metal_mount_normalize(guest_path, norm, sizeof(norm)) != 0) {
		fprintf(stderr, "pm_metal_mount: bad guest path\n");
		return -1;
	}
	ops = pm_metal_mount_resolve_kind(kind);
	if (!ops || !ops->establish) {
		fprintf(stderr, "pm_metal_mount: unknown kind\n");
		return -1;
	}
	if (ops->establish(source, opts, host_path, sizeof(host_path)) != 0) {
		fprintf(stderr, "pm_metal_mount: establish failed for %s\n", norm);
		return -1;
	}
	host_len = strlen(host_path);
	if (host_len + 1 > PM_METAL_MOUNT_HOST_PATH_MAX) {
		fprintf(stderr, "pm_metal_mount: host path too long for %s\n", norm);
		if (ops->release) {
			ops->release(host_path);
		}
		return -1;
	}

	/* Last-mount-at-this-path-wins (see table.h) — release the previous
	 * kind's backing before overwriting the slot. */
	slot = pm_metal_mount_find_exact(norm);
	if (slot) {
		const pm_metal_mount_ops_t *old_ops = pm_metal_mount_resolve_kind(slot->kind);

		if (old_ops && old_ops->release) {
			old_ops->release(slot->host_path);
		}
	} else {
		slot = pm_metal_mount_find_free_slot();
		if (!slot) {
			fprintf(stderr, "pm_metal_mount: table full\n");
			if (ops->release) {
				ops->release(host_path);
			}
			return -1;
		}
	}

	memcpy(slot->guest_path, norm, sizeof(norm));
	memcpy(slot->host_path, host_path, host_len + 1);
	if (source && source[0]) {
		snprintf(slot->source, sizeof(slot->source), "%s", source);
	} else {
		slot->source[0] = '\0';
	}
	if (opts && opts[0]) {
		snprintf(slot->opts, sizeof(slot->opts), "%s", opts);
	} else {
		slot->opts[0] = '\0';
	}
	slot->kind = kind;
	slot->readonly = pm_metal_mount_opts_has(opts, "ro") ? 1 : 0;
	slot->used = 1;

	return 0;
}

int pm_metal_umount(const char *guest_path)
{
	char norm[PM_METAL_MOUNT_GUEST_PATH_MAX];
	pm_metal_mount_entry_t *slot;
	const pm_metal_mount_ops_t *ops;

	if (pm_metal_mount_normalize(guest_path, norm, sizeof(norm)) != 0) {
		return -1;
	}
	slot = pm_metal_mount_find_exact(norm);
	if (!slot) {
		return -1;
	}
	ops = pm_metal_mount_resolve_kind(slot->kind);
	if (ops && ops->release) {
		ops->release(slot->host_path);
	}
	memset(slot, 0, sizeof(*slot));
	return 0;
}

int pm_metal_mount_resolve_ex(const char *guest_path, pm_metal_mount_resolve_t *out)
{
	char norm[PM_METAL_MOUNT_GUEST_PATH_MAX];
	const pm_metal_mount_entry_t *best;
	const char *remainder;
	size_t plen;
	int n;

	if (!out) {
		return -1;
	}
	memset(out, 0, sizeof(*out));
	if (pm_metal_mount_normalize(guest_path, norm, sizeof(norm)) != 0) {
		return -1;
	}
	best = pm_metal_mount_find_best(norm);
	if (!best) {
		return -1;
	}

	plen = strlen(best->guest_path);
	if (plen == 1) {
		remainder = norm + 1;
	} else if (norm[plen] == '/') {
		remainder = norm + plen + 1;
	} else {
		remainder = norm + plen;
	}

	out->kind = best->kind;
	snprintf(out->guest_mount, sizeof(out->guest_mount), "%s", best->guest_path);
	snprintf(out->host_base, sizeof(out->host_base), "%s", best->host_path);
	snprintf(out->remainder, sizeof(out->remainder), "%s", remainder);

	if (best->kind == PM_METAL_MOUNT_PROC) {
		snprintf(out->host_path, sizeof(out->host_path), "%s", best->host_path);
		return 0;
	}

	n = snprintf(out->host_path, sizeof(out->host_path), "%s%s%s", best->host_path,
		     remainder[0] ? "/" : "", remainder);
	return (n > 0 && (size_t)n < sizeof(out->host_path)) ? 0 : -1;
}

int pm_metal_mount_resolve(const char *guest_path, char *out_host_path, size_t out_cap)
{
	pm_metal_mount_resolve_t r;
	int n;

	if (!out_host_path) {
		return -1;
	}
	if (pm_metal_mount_resolve_ex(guest_path, &r) != 0) {
		return -1;
	}
	n = snprintf(out_host_path, out_cap, "%s", r.host_path);
	return (n > 0 && (size_t)n < out_cap) ? 0 : -1;
}

int pm_metal_mount_find_by_host(const char *host_path, char *out_guest, size_t guest_cap,
				 pm_metal_mount_kind_t *out_kind)
{
	int i;

	if (!host_path || !host_path[0] || !out_guest || !out_kind) {
		return -1;
	}
	for (i = 0; i < PM_METAL_MOUNT_MAX; i++) {
		const pm_metal_mount_entry_t *e = &g_pm_metal_mount_table[i];

		if (!e->used) {
			continue;
		}
		if (strcmp(e->host_path, host_path) == 0) {
			snprintf(out_guest, guest_cap, "%s", e->guest_path);
			*out_kind = e->kind;
			return 0;
		}
	}
	return -1;
}

int pm_metal_mount_build_map_dir_list(char out_bufs[][PM_METAL_MOUNT_MAP_DIR_ENTRY_MAX], size_t max_entries,
				       size_t *out_count)
{
	size_t count = 0;
	int i;

	if (!out_bufs || !out_count) {
		return -1;
	}
	for (i = 0; i < PM_METAL_MOUNT_MAX; i++) {
		const pm_metal_mount_entry_t *e = &g_pm_metal_mount_table[i];
		int n;

		if (!e->used) {
			continue;
		}
		if (count >= max_entries) {
			return -1;
		}
		n = snprintf(out_bufs[count], PM_METAL_MOUNT_MAP_DIR_ENTRY_MAX, "%s::%s", e->guest_path,
			     e->host_path);
		if (n <= 0 || (size_t)n >= PM_METAL_MOUNT_MAP_DIR_ENTRY_MAX) {
			return -1;
		}
		count++;
	}
	*out_count = count;
	return 0;
}

void pm_metal_mount_shutdown_all(void)
{
	int i;

	for (i = 0; i < PM_METAL_MOUNT_MAX; i++) {
		pm_metal_mount_entry_t *e = &g_pm_metal_mount_table[i];

		if (e->used) {
			const pm_metal_mount_ops_t *ops = pm_metal_mount_resolve_kind(e->kind);

			if (ops && ops->release) {
				ops->release(e->host_path);
			}
		}
	}
	memset(g_pm_metal_mount_table, 0, sizeof(g_pm_metal_mount_table));
}

int pm_metal_mount_exists(const char *guest_path)
{
	char norm[PM_METAL_MOUNT_GUEST_PATH_MAX];

	if (pm_metal_mount_normalize(guest_path, norm, sizeof(norm)) != 0) {
		return 0;
	}
	return pm_metal_mount_find_exact(norm) != NULL ? 1 : 0;
}

void pm_metal_mount_foreach(pm_metal_mount_foreach_fn fn, void *ctx)
{
	int i;

	if (!fn) {
		return;
	}
	for (i = 0; i < PM_METAL_MOUNT_MAX; i++) {
		const pm_metal_mount_entry_t *e = &g_pm_metal_mount_table[i];

		if (!e->used) {
			continue;
		}
		fn(e->guest_path, e->source, e->host_path, e->kind, e->opts, e->readonly, ctx);
	}
}
