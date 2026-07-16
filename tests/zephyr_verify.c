/*
 * Boot-smoke suite for the Zephyr bring-up binary — staged embeds, batches,
 * process/socket. Exercises common runtime/process APIs; Zephyr-only glue is
 * FatFs staging + k_yield/k_msleep for native_sim. Owns process/runtime
 * teardown on the verify path (main must not double-shutdown).
 */
#include <stdio.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>

#include "pymergetic/metal/mount/fstab.h"
#include "pymergetic/metal/mount/ops.h"
#include "pymergetic/metal/mount/populate.h"
#include "pymergetic/metal/mount/table.h"
#include "pymergetic/metal/port/platform.h"
#include "pymergetic/metal/runtime/process.h"
#include "pymergetic/metal/runtime/runtime.h"

#include "generated/mods_embed.h"
#include "zephyr_verify.h"

/* Bounded try_wait then kill+wait so a stuck peer cannot hang verify forever. */
static void pm_metal_zephyr_reap(pm_metal_process_id_t pid)
{
	int exit_code = -1;
	int i;

	for (i = 0; i < 200; i++) {
		int st = pm_metal_process_try_wait(pid, &exit_code);

		if (st > 0) {
			return;
		}
		if (st < 0) {
			break;
		}
		k_yield();
		if ((i & 15) == 15) {
			k_msleep(1);
		}
	}
	(void)pm_metal_process_kill(pid);
	(void)pm_metal_process_wait(pid, &exit_code);
}

static void pm_metal_zephyr_unload(pm_metal_runtime_handle_t h,
				    pm_metal_process_id_t *maybe_pid)
{
	if (maybe_pid) {
		pm_metal_zephyr_reap(*maybe_pid);
	}
	if (pm_metal_runtime_unload(h) != 0 && maybe_pid) {
		pm_metal_zephyr_reap(*maybe_pid);
		(void)pm_metal_runtime_unload(h);
	}
}

static int pm_metal_zephyr_stage_file(const char *path, const uint8_t *data, uint32_t len)
{
	struct fs_file_t file;
	int rc;
	ssize_t n;

	rc = pm_metal_port_mkdir("/RAM:/mods");
	(void)rc;
	rc = pm_metal_port_mkdir("/RAM:/etc");
	(void)rc;

	fs_file_t_init(&file);
	rc = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE);
	if (rc < 0) {
		printk("stage: open %s failed (%d)\n", path, rc);
		return -1;
	}
	n = len ? fs_write(&file, data, len) : 0;
	fs_close(&file);
	if (n < 0 || (uint32_t)n != len) {
		printk("stage: write %s failed\n", path);
		return -1;
	}
	if (!pm_metal_port_file_exists(path)) {
		printk("stage: %s missing after write\n", path);
		return -1;
	}
	return 0;
}

/*
 * Like pm_metal_app_run_scripted, but does not tear down process/runtime —
 * Zephyr runs several batches in one boot. Stage B (fstab/proc/populate) runs
 * only on the first call.
 */
static int g_pm_metal_zephyr_stage_b_done;

static int pm_metal_zephyr_run_batch(const char *label, int n, char **mods)
{
	int i;
	int rc = 0;

	if (!g_pm_metal_zephyr_stage_b_done) {
		pm_metal_mount_fstab_apply("/etc/fstab");
		if (!pm_metal_mount_exists("/proc")) {
			(void)pm_metal_mount("/proc", PM_METAL_MOUNT_PROC, "proc", NULL);
		}
		pm_metal_mount_populate_all();
		g_pm_metal_zephyr_stage_b_done = 1;
	}

	for (i = 0; i < n; i++) {
		const char *path = mods[i];
		pm_metal_runtime_handle_t h;
		pm_metal_process_id_t pid;
		char *mod_argv[1];
		int exit_code;

		if (pm_metal_runtime_load_file(path, &h) != 0) {
			printk("%s: load failed: %s\n", label, path);
			rc = 1;
			continue;
		}
		mod_argv[0] = (char *)path;
		{
			const char *slash = strrchr(path, '/');

			if (slash && slash[1]) {
				mod_argv[0] = (char *)(slash + 1);
			}
		}
		if (pm_metal_process_spawn(h, 1, mod_argv, 0, NULL, -1, -1, -1, NULL, NULL, &pid)
		    != 0) {
			printk("%s: spawn failed: %s\n", label, path);
			exit_code = -1;
			pm_metal_zephyr_unload(h, NULL);
		} else if (pm_metal_process_wait(pid, &exit_code) != 0) {
			printk("%s: wait failed: %s\n", label, path);
			exit_code = -1;
			pm_metal_zephyr_unload(h, &pid);
		} else {
			printk("%s: exit=%d\n", path, exit_code);
			pm_metal_zephyr_unload(h, NULL);
		}
		if (exit_code != 0) {
			rc = 1;
		}
	}

	printk("verify: %s exit=%d\n", label, rc);
	return rc;
}

static int pm_metal_zephyr_process_smoke(void)
{
	pm_metal_runtime_handle_t h;
	pm_metal_process_id_t pid;
	char *argv1[] = { "t4_getpid" };
	int exit_code = -1;

	if (pm_metal_runtime_load_file("/mods/t4_getpid.wasm", &h) != 0) {
		printk("verify: process load t4 failed\n");
		return -1;
	}
	if (pm_metal_process_spawn(h, 1, argv1, 0, NULL, -1, -1, -1, NULL, NULL, &pid) != 0) {
		printk("verify: process spawn t4 failed\n");
		pm_metal_runtime_unload(h);
		return -1;
	}
	if (pm_metal_process_wait(pid, &exit_code) != 0 || exit_code != 0) {
		printk("verify: process wait t4 failed exit=%d\n", exit_code);
		pm_metal_zephyr_unload(h, &pid);
		return -1;
	}
	printk("verify: process t4_getpid pid=%u exit=%d\n", pid.pid, exit_code);
	pm_metal_runtime_unload(h);

	/* kill() must stop a still-running hot loop (mods/t5_spin). */
	if (pm_metal_runtime_load_file("/mods/t5_spin.wasm", &h) != 0) {
		printk("verify: process load t5 failed\n");
		return -1;
	}
	{
		char *argv_spin[] = { "t5_spin" };
		pm_metal_process_id_t spin_pid;
		int still;
		int spin_exit = 0;
		int i;

		if (pm_metal_process_spawn(h, 1, argv_spin, 0, NULL, -1, -1, -1, NULL, NULL,
					    &spin_pid)
		    != 0) {
			printk("verify: process spawn t5 failed\n");
			pm_metal_runtime_unload(h);
			return -1;
		}
		/*
		 * Wait until run_ex() has published exec.inst (kill before
		 * that is a documented no-op). Yield-poll — workers run at
		 * the same priority as main and WAMR patch 0005 yields on
		 * BR, so this makes progress on native_sim (k_msleep cannot:
		 * simulated time does not advance under a busy guest).
		 */
		for (i = 0; i < 100000; i++) {
			if (pm_metal_process_exec_live(spin_pid)) {
				break;
			}
			still = pm_metal_process_try_wait(spin_pid, &spin_exit);
			if (still < 0) {
				printk("verify: t5 try_wait error before kill\n");
				pm_metal_zephyr_unload(h, &spin_pid);
				return -1;
			}
			if (still > 0) {
				printk("verify: t5 finished before kill (exit=%d)\n", spin_exit);
				pm_metal_runtime_unload(h);
				return -1;
			}
			k_yield();
		}
		if (!pm_metal_process_exec_live(spin_pid)) {
			printk("verify: t5 exec never went live before kill\n");
			(void)pm_metal_process_kill(spin_pid);
			(void)pm_metal_process_wait(spin_pid, &spin_exit);
			pm_metal_runtime_unload(h);
			return -1;
		}
		/* WAMR prints "Exception: terminated by user" — that is the kill. */
		printk("verify: process killing t5_spin (expected Exception follows)\n");
		if (pm_metal_process_kill(spin_pid) != 0) {
			printk("verify: process kill t5 failed\n");
			pm_metal_runtime_unload(h);
			return -1;
		}
		if (pm_metal_process_wait(spin_pid, &spin_exit) != 0) {
			printk("verify: process wait t5 after kill failed\n");
			pm_metal_zephyr_unload(h, &spin_pid);
			return -1;
		}
		if (spin_exit == 0) {
			printk("verify: t5 exited 0 — kill() proved nothing\n");
			pm_metal_runtime_unload(h);
			return -1;
		}
		printk("verify: process t5_spin killed exit=%d (expected)\n", spin_exit);
	}
	pm_metal_runtime_unload(h);
	return 0;
}

/*
 * Spawn server then client (or a single mod), wait for exit 0 on each.
 * Socket mods use their own bounded retry loops for listen/connect races.
 */
static int pm_metal_zephyr_socket_pair(const char *server_mod, const char *client_mod)
{
	pm_metal_runtime_handle_t h_server, h_client;
	pm_metal_process_id_t server_pid, client_pid;
	char *server_argv[1];
	char *client_argv[1];
	char server_path[64];
	char client_path[64];
	int server_exit = -1;
	int client_exit = -1;

	snprintf(server_path, sizeof(server_path), "/mods/%s.wasm", server_mod);
	snprintf(client_path, sizeof(client_path), "/mods/%s.wasm", client_mod);
	server_argv[0] = (char *)server_mod;
	client_argv[0] = (char *)client_mod;

	if (pm_metal_runtime_load_file(server_path, &h_server) != 0) {
		printk("verify: sockets load %s failed\n", server_mod);
		return -1;
	}
	if (pm_metal_runtime_load_file(client_path, &h_client) != 0) {
		printk("verify: sockets load %s failed\n", client_mod);
		pm_metal_runtime_unload(h_server);
		return -1;
	}
	if (pm_metal_process_spawn(h_server, 1, server_argv, 0, NULL, -1, -1, -1, NULL, NULL,
				    &server_pid)
	    != 0) {
		printk("verify: sockets spawn %s failed\n", server_mod);
		pm_metal_runtime_unload(h_client);
		pm_metal_runtime_unload(h_server);
		return -1;
	}
	if (pm_metal_process_spawn(h_client, 1, client_argv, 0, NULL, -1, -1, -1, NULL, NULL,
				    &client_pid)
	    != 0) {
		printk("verify: sockets spawn %s failed\n", client_mod);
		pm_metal_zephyr_reap(server_pid);
		pm_metal_zephyr_unload(h_client, NULL);
		pm_metal_zephyr_unload(h_server, NULL);
		return -1;
	}
	/* Interleave try_wait so a blocked server cannot starve the client.
	 * Mix k_yield with short sleeps: native_sim only advances net timers
	 * when time moves; pure yield spun out before TCP/UDP completed. */
	{
		int server_done = 0;
		int client_done = 0;
		int i;

		for (i = 0; i < 30000 && (!server_done || !client_done); i++) {
			int st;

			if (!server_done) {
				st = pm_metal_process_try_wait(server_pid, &server_exit);
				if (st < 0) {
					printk("verify: sockets try_wait %s failed\n",
					       server_mod);
					pm_metal_zephyr_reap(client_pid);
					pm_metal_zephyr_reap(server_pid);
					pm_metal_zephyr_unload(h_client, NULL);
					pm_metal_zephyr_unload(h_server, NULL);
					return -1;
				}
				if (st > 0) {
					server_done = 1;
				}
			}
			if (!client_done) {
				st = pm_metal_process_try_wait(client_pid, &client_exit);
				if (st < 0) {
					printk("verify: sockets try_wait %s failed\n",
					       client_mod);
					pm_metal_zephyr_reap(client_pid);
					pm_metal_zephyr_reap(server_pid);
					pm_metal_zephyr_unload(h_client, NULL);
					pm_metal_zephyr_unload(h_server, NULL);
					return -1;
				}
				if (st > 0) {
					client_done = 1;
				}
			}
			k_yield();
			if ((i & 15) == 15) {
				k_msleep(1);
			}
		}
		if (!server_done || !client_done) {
			printk("verify: sockets timeout %s/%s (server_done=%d client_done=%d)\n",
			       server_mod, client_mod, server_done, client_done);
			pm_metal_zephyr_reap(server_pid);
			pm_metal_zephyr_reap(client_pid);
			pm_metal_zephyr_unload(h_client, NULL);
			pm_metal_zephyr_unload(h_server, NULL);
			return -1;
		}
	}
	pm_metal_zephyr_unload(h_client, NULL);
	pm_metal_zephyr_unload(h_server, NULL);
	printk("verify: sockets %s/%s exit=%d/%d\n", server_mod, client_mod, server_exit,
	       client_exit);
	if (server_exit != 0 || client_exit != 0) {
		return -1;
	}
	return 0;
}

static int pm_metal_zephyr_socket_one(const char *mod)
{
	pm_metal_runtime_handle_t h;
	pm_metal_process_id_t pid;
	char *argv1[1];
	char path[64];
	int exit_code = -1;

	snprintf(path, sizeof(path), "/mods/%s.wasm", mod);
	argv1[0] = (char *)mod;

	if (pm_metal_runtime_load_file(path, &h) != 0) {
		printk("verify: sockets load %s failed\n", mod);
		return -1;
	}
	if (pm_metal_process_spawn(h, 1, argv1, 0, NULL, -1, -1, -1, NULL, NULL, &pid) != 0) {
		printk("verify: sockets spawn %s failed\n", mod);
		pm_metal_runtime_unload(h);
		return -1;
	}
	if (pm_metal_process_wait(pid, &exit_code) != 0 || exit_code != 0) {
		printk("verify: sockets %s exit=%d\n", mod, exit_code);
		pm_metal_zephyr_unload(h, &pid);
		return -1;
	}
	printk("verify: sockets %s exit=0\n", mod);
	pm_metal_runtime_unload(h);
	return 0;
}

static int pm_metal_zephyr_socket_smoke(void)
{
	if (pm_metal_zephyr_socket_pair("t10_socket_server", "t11_socket_client") != 0) {
		return -1;
	}
	if (pm_metal_zephyr_socket_pair("t24_udp_server", "t25_udp_client") != 0) {
		return -1;
	}
	if (pm_metal_zephyr_socket_pair("t26_ipv6_server", "t27_ipv6_client") != 0) {
		return -1;
	}
	if (pm_metal_zephyr_socket_one("t28_dns_lookup") != 0) {
		return -1;
	}
	printk("verify: sockets tcp/udp/ipv6/dns ok\n");
	return 0;
}

int pm_metal_zephyr_verify(void)
{
	static const char readme[] = "hello from zephyr vfs\n";
	static const char fstab[] =
		"# <source>   <target>    <fstype>   <options>\n"
		"scratch      /scratch    tmpfs      rw\n"
		"scratch      /scratchB   tmpfs      rw\n"
		"other        /other      tmpfs      rw\n";

	char *populate[] = { "/mods/t16_populate_read.wasm" };
	char *tmpfs_ok[] = {
		"/mods/t12_tmpfs_write.wasm",
		"/mods/t13_tmpfs_read.wasm",
		"/mods/t14_tmpfs_read_alt.wasm",
	};
	char *tmpfs_indep[] = { "/mods/t15_tmpfs_read_other.wasm" };
	/* Match verify-linux.sh scripted set: t0/t1 + util natives + guest pthread. */
	char *basic[] = { "/mods/t0_hello.wasm", "/mods/t1_read.wasm" };
	char *utils[] = { "/mods/t3_util_native.wasm", "/mods/t23_pthread.wasm" };
	char *proc[] = { "/mods/t21_proc_mounts.wasm" };
	char *multimod[] = { "/mods/t9_multimod_app.wasm" };
	int rc;
	int failures = 0;

	if (pm_metal_port_mkdir("/RAM:/etc") != 0
	    || pm_metal_port_write_file("/RAM:/README", (const uint8_t *)readme,
					(uint32_t)(sizeof(readme) - 1)) != 0
	    || pm_metal_port_write_file("/RAM:/etc/fstab", (const uint8_t *)fstab,
					(uint32_t)(sizeof(fstab) - 1)) != 0) {
		printk("stage root files failed\n");
		failures++;
		goto verify_done;
	}

#define STAGE(name)                                                                                \
	do {                                                                                       \
		if (pm_metal_zephyr_stage_file("/RAM:/mods/" #name ".wasm",                        \
					       pm_metal_embed_##name,                              \
					       pm_metal_embed_##name##_len) != 0) {                \
			printk("stage " #name " failed\n");                                        \
			failures++;                                                                \
			goto verify_done;                                                          \
		}                                                                                  \
	} while (0)

	STAGE(t0_hello);
	STAGE(t1_read);
	STAGE(t3_util_native);
	STAGE(t12_tmpfs_write);
	STAGE(t13_tmpfs_read);
	STAGE(t14_tmpfs_read_alt);
	STAGE(t15_tmpfs_read_other);
	STAGE(t16_populate_read);
	STAGE(t21_proc_mounts);
	STAGE(t8_multimod_lib);
	STAGE(t9_multimod_app);
	STAGE(t4_getpid);
	STAGE(t5_spin);
	STAGE(t10_socket_server);
	STAGE(t11_socket_client);
	STAGE(t23_pthread);
	STAGE(t24_udp_server);
	STAGE(t25_udp_client);
	STAGE(t26_ipv6_server);
	STAGE(t27_ipv6_client);
	STAGE(t28_dns_lookup);
#undef STAGE

	rc = pm_metal_zephyr_run_batch("basic", 2, basic);
	if (rc != 0) {
		failures++;
	}

	rc = pm_metal_zephyr_run_batch("utils", 2, utils);
	if (rc != 0) {
		failures++;
	}

	/* Populate before tmpfs writers so t16 sees populate content. */
	rc = pm_metal_zephyr_run_batch("populate", 1, populate);
	if (rc != 0) {
		failures++;
	}

	rc = pm_metal_zephyr_run_batch("tmpfs", 3, tmpfs_ok);
	if (rc != 0) {
		failures++;
	}

	/* t15 exits 0 when open on /other fails (isolation). Zephyr FS also
	 * logs <err> open (-2) for that expected miss. */
	printk("verify: tmpfs-indep next open fail is expected\n");
	rc = pm_metal_zephyr_run_batch("tmpfs-indep", 1, tmpfs_indep);
	if (rc != 0) {
		printk("verify: tmpfs independence failed (t15 should exit 0)\n");
		failures++;
	} else {
		printk("verify: tmpfs-indep expected-fail ok\n");
	}

	rc = pm_metal_zephyr_run_batch("proc", 1, proc);
	if (rc != 0) {
		failures++;
	}

	rc = pm_metal_zephyr_run_batch("multimod", 1, multimod);
	if (rc != 0) {
		failures++;
	}

	if (pm_metal_zephyr_process_smoke() != 0) {
		failures++;
	}

	if (pm_metal_zephyr_socket_smoke() != 0) {
		failures++;
	}

verify_done:
	printk("verify: scripted exit=%d\n", failures);
	pm_metal_process_shutdown();
	pm_metal_runtime_shutdown();
	return failures == 0 ? 0 : 1;
}
