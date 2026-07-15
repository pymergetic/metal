#include "pymergetic/metal/mount/proc/mounts.h"

#include <stdio.h>

#include "pymergetic/metal/mount/ops.h"
#include "pymergetic/metal/mount/table.h"

typedef struct pm_metal_mount_proc_mounts_ctx {
	char *out;
	size_t cap;
	size_t used;
	int overflow;
} pm_metal_mount_proc_mounts_ctx_t;

static void pm_metal_mount_proc_mounts_line(const char *guest_path, const char *source,
					    const char *host_path, pm_metal_mount_kind_t kind,
					    const char *opts, int readonly, void *vctx)
{
	pm_metal_mount_proc_mounts_ctx_t *ctx = vctx;
	const char *fstype = pm_metal_mount_kind_name(kind);
	const char *src = (source && source[0]) ? source : "none";
	const char *opt_str;
	char opt_buf[PM_METAL_MOUNT_OPTS_MAX + 8];
	int n;

	(void)host_path;
	if (!fstype) {
		fstype = "unknown";
	}
	if (opts && opts[0]) {
		opt_str = opts;
	} else {
		snprintf(opt_buf, sizeof(opt_buf), "%s", readonly ? "ro" : "rw");
		opt_str = opt_buf;
	}

	n = snprintf(ctx->out + ctx->used, ctx->cap > ctx->used ? ctx->cap - ctx->used : 0, "%s %s %s %s 0 0\n",
		     src, guest_path, fstype, opt_str);
	if (n < 0 || (size_t)n >= (ctx->cap > ctx->used ? ctx->cap - ctx->used : 0)) {
		ctx->overflow = 1;
		return;
	}
	ctx->used += (size_t)n;
}

int pm_metal_mount_proc_generate_mounts(char *out, size_t cap, size_t *out_len)
{
	pm_metal_mount_proc_mounts_ctx_t ctx = {
		.out = out,
		.cap = cap,
		.used = 0,
		.overflow = 0,
	};

	if (!out || !out_len || cap == 0) {
		return -1;
	}
	out[0] = '\0';
	pm_metal_mount_foreach(pm_metal_mount_proc_mounts_line, &ctx);
	if (ctx.overflow) {
		return -1;
	}
	*out_len = ctx.used;
	return 0;
}
