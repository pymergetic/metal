/*
 * NuttX runtime entry — arg parsing -> init -> app/app.h's scripted run
 * mode -> shutdown. See docs/RUNTIME.md, docs/MOUNT.md.
 *
 * Usage: pm-nuttx-runtime --memory=<bytes> --bytecode-memory=<bytes>
 *                          --rootfs=<fstype>:<source>
 *                          [--mount=<fstype>:<source>:<target>[:opts]]...
 *                          [--addr-pool=<cidr>]... [--ns-lookup-pool=<host>]...
 *                          [--env=KEY=VAL]... /mod.wasm [...]
 *                          | -- [--env=KEY=VAL]... /mod.wasm [guest argv...]
 *
 * --memory sizes the kheap pool (wasm linear memory + WAMR's own runtime
 * structs); --bytecode-memory sizes the separate arena raw .wasm module
 * buffers are read into (see pymergetic/metal/memory/{kheap,bytecode}.h) —
 * the two never share space, so a big mod can't starve WAMR's own
 * bookkeeping or vice versa.
 *
 * --rootfs=<fstype>:<source> is Stage A (see docs/MOUNT.md "Boot
 * sequence") — today only <fstype>=hostdir is implemented, <source> a
 * real host directory, resolved to an absolute path via realpath()
 * (POSIX-only) before it ever reaches the runtime. --vfs-root=<dir> is
 * kept as a deprecated alias for --rootfs=hostdir:<dir>. --mount=
 * (repeatable, Stage B CLI sugar — see mount/fstab.h) is applied after
 * any real /etc/fstab on the just-mounted root, before any mod loads.
 *
 * --addr-pool/--ns-lookup-pool (each repeatable, both optional) are the
 * CLI door onto runtime.h's own cfg->addr_pool/ns_lookup_pool — see that
 * struct's own doc comment and docs/RUNTIME.md "Sockets". Neither flag
 * given at all means no guest gets any socket access this whole run,
 * which is why every mod in scripts/verify nuttx sim keeps working
 * unchanged without ever passing either.
 *
 * Each positional .wasm path is guest-style, resolved against the mount
 * table by the runtime (see pm_metal_runtime_load_file) — loaded, run once
 * (argv[0] = its basename), and unloaded, all inside a single
 * init()/shutdown() pair. With `--`, only one .wasm is allowed and
 * remaining args become that guest's argv; `--env=KEY=VAL` (repeatable)
 * is passed through WASI env (needed for PYTHONPATH etc).
 *
 * This file itself only does NuttX entry / argv parsing (incl. resolving a
 * hostdir --rootfs=/--vfs-root= to an absolute path via realpath()) into a
 * pm_metal_runtime_config_t + a pm_metal_app_cli_mount_t list, then hands
 * off to pymergetic/metal/app/app.h — impl: common, same as linux main.
 *
 * On NuttX sim, nsh/`CONFIG_INIT_ARGS` can pass the same flag set the
 * linux CLI uses; see src/nuttx/README.md.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "pymergetic/metal/app/app.h"
#include "pymergetic/metal/memory/memory.h"
#include "pymergetic/metal/mount/ops.h"
#include "pymergetic/metal/runtime/process.h"
#include "pymergetic/metal/runtime/runtime.h"
#include "pymergetic/metal/util/size.h"

static void print_usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s [--memory=<bytes>] [--bytecode-memory=<bytes>] --rootfs=<fstype>:<source>\n"
		"       [--mount=<fstype>:<source>:<target>[:opts]]... [--addr-pool=<cidr>]...\n"
		"       [--ns-lookup-pool=<host>]... [--env=KEY=VAL]...\n"
		"       [--stack-memory=<bytes>] [--allow-guest-mount]\n"
		"       /mod.wasm [...] | -- /mod.wasm [guest argv...]\n"
		"  (--vfs-root=<dir> is a deprecated alias for --rootfs=hostdir:<dir>)\n"
		"  --memory=/--bytecode-memory=/--stack-memory=: omit for server layout\n"
		"    (256 MiB kheap / 64 MiB bytecode / 16 MiB stack — memory/layout.h)\n"
		"  --allow-guest-mount: permit guest mount()/umount() natives (default deny)\n"
		"  -- : single-guest mode — first path is the .wasm, rest are guest argv\n",
		argv0);
}

/* In-place ':'-splitter for --mount=<fstype>:<source>:<target>[:opts] —
 * mutates `arg` (NULs inserted at each of the first 3 colons, same
 * convention as strtok()); a trailing opts field is left whole even if
 * it contains further colons of its own (fstab options are comma-
 * separated, never colon-separated, but this stays permissive rather
 * than silently mis-splitting one that did). Returns 0/-1 (fewer than 3
 * colon-separated fields). */
static int split_mount_arg(char *arg, const char **fstype, const char **source, const char **target,
			    const char **opts)
{
	char *p = arg;
	char *colon;

	*fstype = p;
	colon = strchr(p, ':');
	if (!colon) {
		return -1;
	}
	*colon = '\0';
	p = colon + 1;

	*source = p;
	colon = strchr(p, ':');
	if (!colon) {
		return -1;
	}
	*colon = '\0';
	p = colon + 1;

	*target = p;
	colon = strchr(p, ':');
	if (colon) {
		*colon = '\0';
		*opts = colon + 1;
	} else {
		*opts = NULL;
	}
	return 0;
}

/* Same amortized-growth shape as append_string() below, for
 * --mount='s own struct-per-occurrence instead of one string. */
static int append_cli_mount(pm_metal_app_cli_mount_t **arr, uint32_t *count, const char *fstype,
			     const char *source, const char *target, const char *opts)
{
	pm_metal_app_cli_mount_t *grown =
		realloc((void *)*arr, sizeof(**arr) * (size_t)(*count + 1));

	if (!grown) {
		return -1;
	}
	grown[*count].fstype = fstype;
	grown[*count].source = source;
	grown[*count].target = target;
	grown[*count].opts = opts;
	*arr = grown;
	(*count)++;
	return 0;
}

/* Repeatable flags (--addr-pool, --ns-lookup-pool) grow this by one
 * pointer per occurrence — argc itself is always a safe upper bound on
 * how many any single flag could have, so one realloc per hit is enough,
 * no doubling/amortized-growth scheme needed for a CLI-sized list. */
static int append_string(const char ***arr, uint32_t *count, const char *value)
{
	const char **grown = realloc((void *)*arr, sizeof(char *) * (size_t)(*count + 1));

	if (!grown) {
		return -1;
	}
	grown[*count] = value;
	*arr = grown;
	(*count)++;
	return 0;
}

int main(int argc, char **argv)
{
	uint64_t memory_bytes = 0;
	uint64_t bytecode_bytes = 0;
	uint32_t stack_bytes = 0;
	const char *vfs_root_arg = NULL; /* deprecated --vfs-root= alias, see split below */
	const char *rootfs_arg = NULL; /* raw --rootfs=<fstype>:<source>, not yet split */
	const char **addr_pool = NULL;
	uint32_t addr_pool_count = 0;
	const char **ns_lookup_pool = NULL;
	uint32_t ns_lookup_pool_count = 0;
	const char **guest_envp = NULL;
	uint32_t guest_envc = 0;
	pm_metal_app_cli_mount_t *cli_mounts = NULL;
	uint32_t cli_mount_count = 0;
	int allow_guest_mount = 0;
	int single_guest = 0;
	int wasm_argc = 0;
	char **wasm_argv;
	int guest_argc = 0;
	char **guest_argv = NULL;
	int i;

	wasm_argv = malloc(sizeof(char *) * (size_t)(argc > 0 ? argc : 1));
	if (!wasm_argv) {
		return 1;
	}

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--")) {
			single_guest = 1;
			i++;
			if (i >= argc) {
				fprintf(stderr, "%s: -- requires /mod.wasm [guest argv...]\n", argv[0]);
				free(wasm_argv);
				free((void *)addr_pool);
				free((void *)ns_lookup_pool);
				free((void *)guest_envp);
				free(cli_mounts);
				return 1;
			}
			wasm_argv[wasm_argc++] = argv[i];
			guest_argv = malloc(sizeof(char *) * (size_t)(argc - i));
			if (!guest_argv) {
				free(wasm_argv);
				free((void *)addr_pool);
				free((void *)ns_lookup_pool);
				free((void *)guest_envp);
				free(cli_mounts);
				return 1;
			}
			{
				const char *slash = strrchr(argv[i], '/');
				const char *base = slash ? slash + 1 : argv[i];

				guest_argv[guest_argc++] = (char *)base;
			}
			for (i++; i < argc; i++) {
				guest_argv[guest_argc++] = argv[i];
			}
			break;
		} else if (!strncmp(argv[i], "--memory=", 9)) {
			memory_bytes = strtoull(argv[i] + 9, NULL, 0);
		} else if (!strncmp(argv[i], "--bytecode-memory=", 18)) {
			bytecode_bytes = strtoull(argv[i] + 18, NULL, 0);
		} else if (!strncmp(argv[i], "--stack-memory=", 15)) {
			stack_bytes = (uint32_t)strtoul(argv[i] + 15, NULL, 0);
		} else if (!strncmp(argv[i], "--rootfs=", 9)) {
			rootfs_arg = argv[i] + 9;
		} else if (!strncmp(argv[i], "--vfs-root=", 11)) {
			vfs_root_arg = argv[i] + 11;
		} else if (!strncmp(argv[i], "--mount=", 8)) {
			const char *fstype;
			const char *source;
			const char *target;
			const char *opts;

			if (split_mount_arg(argv[i] + 8, &fstype, &source, &target, &opts) != 0) {
				fprintf(stderr, "%s: bad --mount= (want <fstype>:<source>:<target>[:opts]): %s\n",
					argv[0], argv[i] + 8);
				free(wasm_argv);
				free((void *)addr_pool);
				free((void *)ns_lookup_pool);
				free((void *)guest_envp);
				free(cli_mounts);
				return 1;
			}
			if (append_cli_mount(&cli_mounts, &cli_mount_count, fstype, source, target, opts) != 0) {
				free(wasm_argv);
				free((void *)addr_pool);
				free((void *)ns_lookup_pool);
				free((void *)guest_envp);
				free(cli_mounts);
				return 1;
			}
		} else if (!strncmp(argv[i], "--addr-pool=", 12)) {
			if (append_string(&addr_pool, &addr_pool_count, argv[i] + 12) != 0) {
				free(wasm_argv);
				free((void *)addr_pool);
				free((void *)ns_lookup_pool);
				free((void *)guest_envp);
				free(cli_mounts);
				return 1;
			}
		} else if (!strncmp(argv[i], "--ns-lookup-pool=", 17)) {
			if (append_string(&ns_lookup_pool, &ns_lookup_pool_count, argv[i] + 17) != 0) {
				free(wasm_argv);
				free((void *)addr_pool);
				free((void *)ns_lookup_pool);
				free((void *)guest_envp);
				free(cli_mounts);
				return 1;
			}
		} else if (!strncmp(argv[i], "--env=", 6)) {
			if (!strchr(argv[i] + 6, '=')) {
				fprintf(stderr, "%s: bad --env= (want KEY=VAL): %s\n", argv[0], argv[i] + 6);
				free(wasm_argv);
				free((void *)addr_pool);
				free((void *)ns_lookup_pool);
				free((void *)guest_envp);
				free(cli_mounts);
				return 1;
			}
			if (append_string(&guest_envp, &guest_envc, argv[i] + 6) != 0) {
				free(wasm_argv);
				free((void *)addr_pool);
				free((void *)ns_lookup_pool);
				free((void *)guest_envp);
				free(cli_mounts);
				return 1;
			}
		} else if (!strcmp(argv[i], "--allow-guest-mount")) {
			allow_guest_mount = 1;
		} else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
			print_usage(argv[0]);
			free(wasm_argv);
			free((void *)addr_pool);
			free((void *)ns_lookup_pool);
			free((void *)guest_envp);
			free(cli_mounts);
			return 0;
		} else {
			wasm_argv[wasm_argc++] = argv[i];
		}
	}

	if (memory_bytes == 0) {
		memory_bytes = PM_METAL_MEMORY_KHEAP_BYTES;
	}
	if (bytecode_bytes == 0) {
		bytecode_bytes = PM_METAL_MEMORY_BYTECODE_BYTES;
	}
	if (stack_bytes == 0) {
		stack_bytes = PM_METAL_MEMORY_STACK_BYTES;
	}
	if ((!rootfs_arg && !vfs_root_arg) || wasm_argc == 0) {
		print_usage(argv[0]);
		free(wasm_argv);
		free(guest_argv);
		free((void *)addr_pool);
		free((void *)ns_lookup_pool);
		free((void *)guest_envp);
		free(cli_mounts);
		return 1;
	}
	if (rootfs_arg && vfs_root_arg) {
		fprintf(stderr, "%s: pass either --rootfs= or --vfs-root=, not both\n", argv[0]);
		free(wasm_argv);
		free(guest_argv);
		free((void *)addr_pool);
		free((void *)ns_lookup_pool);
		free((void *)guest_envp);
		free(cli_mounts);
		return 1;
	}
	if (guest_envc > 0 && !single_guest && wasm_argc != 1) {
		fprintf(stderr, "%s: --env= with multiple .wasm paths requires -- (single-guest mode)\n",
			argv[0]);
		free(wasm_argv);
		free(guest_argv);
		free((void *)addr_pool);
		free((void *)ns_lookup_pool);
		free((void *)guest_envp);
		free(cli_mounts);
		return 1;
	}

	/* Stage A — see docs/MOUNT.md "Boot sequence". --vfs-root=<dir> is
	 * just --rootfs=hostdir:<dir> under a deprecated name; both end up
	 * here as one (rootfs_fstype, rootfs_source) pair. Only "hostdir" is
	 * implemented today (see mount/hostdir.h) — any other fstype name is
	 * rejected up front rather than reaching pm_metal_runtime_init() and
	 * failing there with a less specific error. */
	char rootfs_split[PATH_MAX];
	const char *rootfs_fstype;
	const char *rootfs_source;
	pm_metal_mount_kind_t rootfs_kind;

	if (vfs_root_arg) {
		rootfs_fstype = "hostdir";
		rootfs_source = vfs_root_arg;
	} else {
		char *colon;

		if (strlen(rootfs_arg) + 1 > sizeof(rootfs_split)) {
			fprintf(stderr, "%s: --rootfs= too long\n", argv[0]);
			free(wasm_argv);
			free(guest_argv);
			free((void *)addr_pool);
			free((void *)ns_lookup_pool);
			free((void *)guest_envp);
			free(cli_mounts);
			return 1;
		}
		memcpy(rootfs_split, rootfs_arg, strlen(rootfs_arg) + 1);
		colon = strchr(rootfs_split, ':');
		if (!colon) {
			fprintf(stderr, "%s: bad --rootfs= (want <fstype>:<source>): %s\n", argv[0], rootfs_arg);
			free(wasm_argv);
			free(guest_argv);
			free((void *)addr_pool);
			free((void *)ns_lookup_pool);
			free((void *)guest_envp);
			free(cli_mounts);
			return 1;
		}
		*colon = '\0';
		rootfs_fstype = rootfs_split;
		rootfs_source = colon + 1;
	}

	if (pm_metal_mount_kind_by_name(rootfs_fstype, &rootfs_kind) != 0 || rootfs_kind != PM_METAL_MOUNT_HOSTDIR) {
		fprintf(stderr,
			"%s: --rootfs= fstype '%s' not yet supported (only hostdir today — see docs/MOUNT.md)\n",
			argv[0], rootfs_fstype);
		free(wasm_argv);
		free(guest_argv);
		free((void *)addr_pool);
		free((void *)ns_lookup_pool);
		free((void *)guest_envp);
		free(cli_mounts);
		return 1;
	}

	char vfs_root_abs[PATH_MAX];
	if (!realpath(rootfs_source, vfs_root_abs)) {
		fprintf(stderr, "%s: bad --rootfs=/--vfs-root= source: %s\n", argv[0], rootfs_source);
		free(wasm_argv);
		free(guest_argv);
		free((void *)addr_pool);
		free((void *)ns_lookup_pool);
		free((void *)guest_envp);
		free(cli_mounts);
		return 1;
	}

	pm_metal_runtime_config_t cfg = {
		.memory_bytes = memory_bytes,
		.bytecode_bytes = bytecode_bytes,
		.stack_bytes = stack_bytes,
		.vfs_root = vfs_root_abs,
		.addr_pool = addr_pool,
		.addr_pool_count = addr_pool_count,
		.ns_lookup_pool = ns_lookup_pool,
		.ns_lookup_pool_count = ns_lookup_pool_count,
	};

	if (pm_metal_runtime_init(&cfg) != 0) {
		fprintf(stderr, "%s: runtime init failed\n", argv[0]);
		free(wasm_argv);
		free(guest_argv);
		free((void *)addr_pool);
		free((void *)ns_lookup_pool);
		free((void *)guest_envp);
		free(cli_mounts);
		return 1;
	}
	pm_metal_runtime_set_allow_guest_mount(allow_guest_mount);
	/* init() already made its own copies of addr_pool/ns_lookup_pool
	 * (see runtime.h's own doc comment on cfg->addr_pool) — this
	 * process's own copy of the flag list is done being useful the
	 * moment that call returns. cli_mounts / guest_envp stay alive until
	 * run_scripted() returns. */
	free((void *)addr_pool);
	free((void *)ns_lookup_pool);
	if (pm_metal_process_init() != 0) {
		fprintf(stderr, "%s: process table init failed\n", argv[0]);
		pm_metal_runtime_shutdown();
		free(wasm_argv);
		free(guest_argv);
		free((void *)guest_envp);
		free(cli_mounts);
		return 1;
	}

	/* machine_ram = total RAM probe (diagnostics); kheap/bytecode pools
	 * are separate — see pymergetic/metal/memory/ and docs/RUNTIME.md. */
	char ram_human[48];
	char pool_human[48];
	char bytecode_human[48];
	pm_metal_util_size_format_bytes(ram_human, sizeof(ram_human), pm_metal_memory_ram_ops()->probe());
	pm_metal_util_size_format_bytes(pool_human, sizeof(pool_human), pm_metal_memory_kheap_ops()->bytes());
	pm_metal_util_size_format_bytes(bytecode_human, sizeof(bytecode_human),
					 pm_metal_memory_bytecode_ops()->bytes());
	printf("machine_ram=%s kheap_pool=%s bytecode_pool=%s rootfs=hostdir:%s\n", ram_human, pool_human,
	       bytecode_human, vfs_root_abs);
	fflush(stdout);

	int rc = pm_metal_app_run_scripted(argv[0], wasm_argc, wasm_argv, cli_mounts, cli_mount_count,
					    guest_argc, guest_argv, (int)guest_envc, guest_envp);

	free(cli_mounts);
	free(guest_argv);
	free((void *)guest_envp);
	free(wasm_argv);
	return rc;
}
