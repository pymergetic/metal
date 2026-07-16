/*
 * Zephyr runtime entry — FAT root → init → scripted verify suite.
 * See docs/RUNTIME.md Bring-up plan §5 and the Zephyr bring-up plan.
 */
#include <stdio.h>
#include <string.h>

#include <ff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/disk_access.h>

#include "pymergetic/metal/memory/budget.h"
#include "pymergetic/metal/memory/memory.h"
#include "pymergetic/metal/mount/fstab.h"
#include "pymergetic/metal/mount/ops.h"
#include "pymergetic/metal/mount/populate.h"
#include "pymergetic/metal/mount/table.h"
#include "pymergetic/metal/port/platform.h"
#include "pymergetic/metal/runtime/process.h"
#include "pymergetic/metal/runtime/runtime.h"
#include "pymergetic/metal/wasi/file.h"

#if IS_ENABLED(CONFIG_PM_METAL_VERIFY_MODS)
#include "generated/mods_embed.h"
#endif

#define PM_METAL_ZEPHYR_VFS_ROOT "/RAM:"
#define PM_METAL_ZEPHYR_MEMORY_REQ (12ull * 1024ull * 1024ull)
#define PM_METAL_ZEPHYR_BYTECODE_REQ (4ull * 1024ull * 1024ull)

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

#if IS_ENABLED(CONFIG_PM_METAL_VERIFY_MODS)
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
		if (pm_metal_process_spawn(h, 1, mod_argv, 0, NULL, -1, -1, -1, NULL, NULL, &pid) != 0
		    || pm_metal_process_wait(pid, &exit_code) != 0) {
			printk("%s: run failed: %s\n", label, path);
			exit_code = -1;
		}
		printk("%s: exit=%d\n", path, exit_code);
		if (exit_code != 0) {
			rc = 1;
		}
		pm_metal_runtime_unload(h);
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
		pm_metal_runtime_unload(h);
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
				pm_metal_runtime_unload(h);
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
		if (pm_metal_process_kill(spin_pid) != 0) {
			printk("verify: process kill t5 failed\n");
			pm_metal_runtime_unload(h);
			return -1;
		}
		if (pm_metal_process_wait(spin_pid, &spin_exit) != 0) {
			printk("verify: process wait t5 after kill failed\n");
			pm_metal_runtime_unload(h);
			return -1;
		}
		if (spin_exit == 0) {
			printk("verify: t5 exited 0 — kill() proved nothing\n");
			pm_metal_runtime_unload(h);
			return -1;
		}
		printk("verify: process t5_spin killed exit=%d\n", spin_exit);
	}
	pm_metal_runtime_unload(h);
	return 0;
}
#endif

int main(void)
{
	pm_metal_runtime_config_t cfg;
	uint64_t machine;
	uint64_t budget;
	int rc;
	int failures = 0;

	printk("runtime: target=zephyr\n");

	/* Stage A: format + mount FAT ramdisk at /RAM: (ELM FatFs volume name). */
	{
		static FATFS fat_fs;
		static struct fs_mount_t fat_mnt = {
			.type = FS_FATFS,
			.fs_data = &fat_fs,
			.mnt_point = "/RAM:",
			.storage_dev = (void *)"RAM",
			.flags = FS_MOUNT_FLAG_USE_DISK_ACCESS,
		};
		struct fs_dir_t dir;
		int frc;

		frc = disk_access_init("RAM");
		if (frc != 0) {
			printk("disk_access_init RAM failed (%d)\n", frc);
			return 1;
		}
		frc = fs_mkfs(FS_FATFS, (uintptr_t)"RAM", NULL, 0);
		if (frc < 0) {
			printk("fat mkfs failed (%d)\n", frc);
			return 1;
		}
		frc = fs_mount(&fat_mnt);
		if (frc < 0) {
			printk("fat mount failed (%d)\n", frc);
			return 1;
		}
		fs_dir_t_init(&dir);
		frc = fs_opendir(&dir, "/RAM:");
		if (frc < 0) {
			printk("fat root unavailable (%d)\n", frc);
			return 1;
		}
		fs_closedir(&dir);
		printk("fat root ok at /RAM:\n");
	}

	if (pm_metal_wasi_file_init(PM_METAL_ZEPHYR_VFS_ROOT) != 0) {
		printk("wasi file init failed\n");
		return 1;
	}

	machine = pm_metal_memory_ram_ops()->probe();
	budget = pm_metal_memory_zephyr_arena_budget();
	printk("runtime: machine_ram=%llu arena_budget=%llu\n",
	       (unsigned long long)machine, (unsigned long long)budget);

	memset(&cfg, 0, sizeof(cfg));
	/* Leave enough bytecode arena for the largest embedded mod (~250 KiB)
	 * plus populate scratch; on qemu E820 the arena is much smaller than
	 * native_sim's static pool. */
	cfg.bytecode_bytes = PM_METAL_ZEPHYR_BYTECODE_REQ;
	if (budget > 0 && cfg.bytecode_bytes + (2ull * 1024ull * 1024ull) > budget) {
		cfg.bytecode_bytes = budget / 4;
		if (cfg.bytecode_bytes < (512ull * 1024ull)) {
			cfg.bytecode_bytes = budget / 2;
		}
	}
	cfg.memory_bytes = (budget > cfg.bytecode_bytes) ? (budget - cfg.bytecode_bytes)
							 : PM_METAL_ZEPHYR_MEMORY_REQ;
	if (cfg.memory_bytes > PM_METAL_ZEPHYR_MEMORY_REQ) {
		cfg.memory_bytes = PM_METAL_ZEPHYR_MEMORY_REQ;
	}
	cfg.vfs_root = PM_METAL_ZEPHYR_VFS_ROOT;

	if (pm_metal_runtime_init(&cfg) != 0) {
		printk("runtime init failed\n");
		return 1;
	}
	if (pm_metal_process_init() != 0) {
		printk("process init failed\n");
		pm_metal_runtime_shutdown();
		return 1;
	}

	printk("runtime: kheap_pool=%llu bytecode_pool=%llu\n",
	       (unsigned long long)pm_metal_memory_kheap_ops()->bytes(),
	       (unsigned long long)pm_metal_memory_bytecode_ops()->bytes());

#if IS_ENABLED(CONFIG_PM_METAL_VERIFY_MODS)
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
		char *basic[] = { "/mods/t0_hello.wasm", "/mods/t1_read.wasm" };
		char *proc[] = { "/mods/t21_proc_mounts.wasm" };
		char *multimod[] = { "/mods/t9_multimod_app.wasm" };

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
#undef STAGE

		rc = pm_metal_zephyr_run_batch("basic", 2, basic);
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

		rc = pm_metal_zephyr_run_batch("tmpfs-indep", 1, tmpfs_indep);
		if (rc == 0) {
			printk("verify: tmpfs independence unexpectedly succeeded\n");
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

verify_done:
		printk("verify: scripted exit=%d\n", failures);
		pm_metal_process_shutdown();
		pm_metal_runtime_shutdown();
		return failures == 0 ? 0 : 1;
	}
#else
	pm_metal_process_shutdown();
	pm_metal_runtime_shutdown();
	return 0;
#endif
}
