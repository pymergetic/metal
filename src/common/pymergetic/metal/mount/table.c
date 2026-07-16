/*
 * Mount table — impl: common. See table.h.
 */
#include "pymergetic/metal/mount/table.h"

#include <stdio.h>
#include <string.h>

#include "pymergetic/metal/port/lock.h"

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
static pm_metal_port_mutex_t g_pm_metal_mount_table_lock;
static int g_pm_metal_mount_table_lock_ready;

static void pm_metal_mount_table_lock(void)
{
	if (!g_pm_metal_mount_table_lock_ready) {
		pm_metal_port_mutex_init(&g_pm_metal_mount_table_lock);
		g_pm_metal_mount_table_lock_ready = 1;
	}
	pm_metal_port_mutex_lock(&g_pm_metal_mount_table_lock);
}

static void pm_metal_mount_table_unlock(void)
{
	pm_metal_port_mutex_unlock(&g_pm_metal_mount_table_lock);
}

/* Lexical absolute normalize: leading "/", drop ".", reject ".." (zip-slip /
 * mount-table escape), collapse repeated "/", strip trailing "/" except root.
 * Same shape wasi-libc expects of a map_dir_list guest-side prefix. */
int pm_metal_mount_normalize(const char *in, char *out, size_t out_cap)
{
	const char *p;
	size_t out_len = 0;

	if (!in || !in[0] || !out || out_cap < 2) {
		return -1;
	}

	out[0] = '/';
	out[1] = '\0';
	out_len = 1;
	p = in;
	while (*p == '/') {
		p++;
	}

	while (*p) {
		const char *start = p;
		size_t comp_len;

		while (*p && *p != '/') {
			p++;
		}
		comp_len = (size_t)(p - start);
		while (*p == '/') {
			p++;
		}
		if (comp_len == 0) {
			continue;
		}
		if (comp_len == 1 && start[0] == '.') {
			continue;
		}
		if (comp_len == 2 && start[0] == '.' && start[1] == '.') {
			return -1;
		}
		/* Append "/comp" — root already has leading '/'. */
		if (out_len > 1) {
			if (out_len + 1 >= out_cap) {
				return -1;
			}
			out[out_len++] = '/';
			out[out_len] = '\0';
		}
		if (out_len + comp_len >= out_cap) {
			return -1;
		}
		memcpy(out + out_len, start, comp_len);
		out_len += comp_len;
		out[out_len] = '\0';
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
	char old_host[PM_METAL_MOUNT_HOST_PATH_MAX];
	pm_metal_mount_kind_t old_kind = 0;
	int have_old = 0;
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
	/* establish() outside the table lock — may block / allocate. */
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

	pm_metal_mount_table_lock();
	slot = pm_metal_mount_find_exact(norm);
	if (slot) {
		const pm_metal_mount_ops_t *old_ops = pm_metal_mount_resolve_kind(slot->kind);

		if (old_ops && old_ops->release) {
			have_old = 1;
			old_kind = slot->kind;
			snprintf(old_host, sizeof(old_host), "%s", slot->host_path);
		}
	} else {
		slot = pm_metal_mount_find_free_slot();
		if (!slot) {
			pm_metal_mount_table_unlock();
			fprintf(stderr, "pm_metal_mount: table full\n");
			if (ops->release) {
				ops->release(host_path);
			}
			return -1;
		}
	}

	memcpy(slot->guest_path, norm, strlen(norm) + 1);
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
	pm_metal_mount_table_unlock();

	if (have_old) {
		const pm_metal_mount_ops_t *old_ops = pm_metal_mount_resolve_kind(old_kind);

		if (old_ops && old_ops->release) {
			old_ops->release(old_host);
		}
	}

	return 0;
}

int pm_metal_umount(const char *guest_path)
{
	char norm[PM_METAL_MOUNT_GUEST_PATH_MAX];
	char host_path[PM_METAL_MOUNT_HOST_PATH_MAX];
	pm_metal_mount_kind_t kind;
	const pm_metal_mount_ops_t *ops;
	pm_metal_mount_entry_t *slot;

	if (pm_metal_mount_normalize(guest_path, norm, sizeof(norm)) != 0) {
		return -1;
	}
	pm_metal_mount_table_lock();
	slot = pm_metal_mount_find_exact(norm);
	if (!slot) {
		pm_metal_mount_table_unlock();
		return -1;
	}
	kind = slot->kind;
	snprintf(host_path, sizeof(host_path), "%s", slot->host_path);
	memset(slot, 0, sizeof(*slot));
	pm_metal_mount_table_unlock();

	ops = pm_metal_mount_resolve_kind(kind);
	if (ops && ops->release) {
		ops->release(host_path);
	}
	return 0;
}

int pm_metal_mount_resolve_ex(const char *guest_path, pm_metal_mount_resolve_t *out)
{
	char norm[PM_METAL_MOUNT_GUEST_PATH_MAX];
	const pm_metal_mount_entry_t *best;
	const char *remainder;
	size_t plen;
	int n;
	int rc = -1;

	if (!out) {
		return -1;
	}
	memset(out, 0, sizeof(*out));
	if (pm_metal_mount_normalize(guest_path, norm, sizeof(norm)) != 0) {
		return -1;
	}
	pm_metal_mount_table_lock();
	best = pm_metal_mount_find_best(norm);
	if (!best) {
		pm_metal_mount_table_unlock();
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
		rc = 0;
	} else {
		n = snprintf(out->host_path, sizeof(out->host_path), "%s%s%s", best->host_path,
			     remainder[0] ? "/" : "", remainder);
		rc = (n > 0 && (size_t)n < sizeof(out->host_path)) ? 0 : -1;
	}
	pm_metal_mount_table_unlock();
	return rc;
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
	int rc = -1;

	if (!host_path || !host_path[0] || !out_guest || !out_kind) {
		return -1;
	}
	pm_metal_mount_table_lock();
	for (i = 0; i < PM_METAL_MOUNT_MAX; i++) {
		const pm_metal_mount_entry_t *e = &g_pm_metal_mount_table[i];

		if (!e->used) {
			continue;
		}
		if (strcmp(e->host_path, host_path) == 0) {
			snprintf(out_guest, guest_cap, "%s", e->guest_path);
			*out_kind = e->kind;
			rc = 0;
			break;
		}
	}
	pm_metal_mount_table_unlock();
	return rc;
}

int pm_metal_mount_build_map_dir_list(char out_bufs[][PM_METAL_MOUNT_MAP_DIR_ENTRY_MAX], size_t max_entries,
				       size_t *out_count)
{
	size_t count = 0;
	size_t need = 0;
	int i;

	if (!out_bufs || !out_count) {
		return -1;
	}
	pm_metal_mount_table_lock();
	for (i = 0; i < PM_METAL_MOUNT_MAX; i++) {
		if (g_pm_metal_mount_table[i].used) {
			need++;
		}
	}
	if (need > max_entries) {
		pm_metal_mount_table_unlock();
		return -1;
	}
	for (i = 0; i < PM_METAL_MOUNT_MAX; i++) {
		const pm_metal_mount_entry_t *e = &g_pm_metal_mount_table[i];
		int n;

		if (!e->used) {
			continue;
		}
		n = snprintf(out_bufs[count], PM_METAL_MOUNT_MAP_DIR_ENTRY_MAX, "%s::%s", e->guest_path,
			     e->host_path);
		if (n <= 0 || (size_t)n >= PM_METAL_MOUNT_MAP_DIR_ENTRY_MAX) {
			pm_metal_mount_table_unlock();
			return -1;
		}
		count++;
	}
	*out_count = count;
	pm_metal_mount_table_unlock();
	return 0;
}

void pm_metal_mount_shutdown_all(void)
{
	struct {
		int used;
		pm_metal_mount_kind_t kind;
		char host_path[PM_METAL_MOUNT_HOST_PATH_MAX];
	} snap[PM_METAL_MOUNT_MAX];
	int i;

	pm_metal_mount_table_lock();
	for (i = 0; i < PM_METAL_MOUNT_MAX; i++) {
		pm_metal_mount_entry_t *e = &g_pm_metal_mount_table[i];

		snap[i].used = e->used;
		if (e->used) {
			snap[i].kind = e->kind;
			snprintf(snap[i].host_path, sizeof(snap[i].host_path), "%s", e->host_path);
		}
	}
	memset(g_pm_metal_mount_table, 0, sizeof(g_pm_metal_mount_table));
	pm_metal_mount_table_unlock();

	for (i = 0; i < PM_METAL_MOUNT_MAX; i++) {
		if (snap[i].used) {
			const pm_metal_mount_ops_t *ops = pm_metal_mount_resolve_kind(snap[i].kind);

			if (ops && ops->release) {
				ops->release(snap[i].host_path);
			}
		}
	}
}

int pm_metal_mount_exists(const char *guest_path)
{
	char norm[PM_METAL_MOUNT_GUEST_PATH_MAX];
	int exists;

	if (pm_metal_mount_normalize(guest_path, norm, sizeof(norm)) != 0) {
		return 0;
	}
	pm_metal_mount_table_lock();
	exists = pm_metal_mount_find_exact(norm) != NULL ? 1 : 0;
	pm_metal_mount_table_unlock();
	return exists;
}

void pm_metal_mount_foreach(pm_metal_mount_foreach_fn fn, void *ctx)
{
	struct {
		int used;
		char guest_path[PM_METAL_MOUNT_GUEST_PATH_MAX];
		char source[PM_METAL_MOUNT_HOST_PATH_MAX];
		char host_path[PM_METAL_MOUNT_HOST_PATH_MAX];
		char opts[PM_METAL_MOUNT_OPTS_MAX];
		pm_metal_mount_kind_t kind;
		int readonly;
	} snap[PM_METAL_MOUNT_MAX];
	int i;

	if (!fn) {
		return;
	}
	pm_metal_mount_table_lock();
	for (i = 0; i < PM_METAL_MOUNT_MAX; i++) {
		const pm_metal_mount_entry_t *e = &g_pm_metal_mount_table[i];

		snap[i].used = e->used;
		if (e->used) {
			snprintf(snap[i].guest_path, sizeof(snap[i].guest_path), "%s", e->guest_path);
			snprintf(snap[i].source, sizeof(snap[i].source), "%s", e->source);
			snprintf(snap[i].host_path, sizeof(snap[i].host_path), "%s", e->host_path);
			snprintf(snap[i].opts, sizeof(snap[i].opts), "%s", e->opts);
			snap[i].kind = e->kind;
			snap[i].readonly = e->readonly;
		}
	}
	pm_metal_mount_table_unlock();

	for (i = 0; i < PM_METAL_MOUNT_MAX; i++) {
		if (!snap[i].used) {
			continue;
		}
		fn(snap[i].guest_path, snap[i].source, snap[i].host_path, snap[i].kind, snap[i].opts,
		   snap[i].readonly, ctx);
	}
}
