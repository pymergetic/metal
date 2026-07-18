/*
 * Zephyr runtime entry — FAT root → init → optional scripted verify suite.
 * See docs/RUNTIME.md Bring-up plan §5 and the Zephyr bring-up plan.
 */
#include <string.h>

#include <ff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/disk_access.h>

#if defined(CONFIG_NET_L2_ETHERNET) && !defined(CONFIG_NET_CONFIG_AUTO_INIT)
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_config.h>
#include <zephyr/net/net_if.h>
#endif

#include "pymergetic/metal/memory/budget.h"
#include "pymergetic/metal/memory/memory.h"
#include "pymergetic/metal/runtime/process.h"
#include "pymergetic/metal/runtime/runtime.h"
#include "pymergetic/metal/wasi/file.h"

#if IS_ENABLED(CONFIG_PM_METAL_VERIFY_MODS)
#include "zephyr_verify.h"
#endif

#define PM_METAL_ZEPHYR_VFS_ROOT "/RAM:"
/* Preferred pool sizes — same server layout as linux/nuttx (memory/layout.h).
 * Caps when arena_budget is smaller; static/MMU pool must be ≥ kheap+bytecode. */

int main(void)
{
	pm_metal_runtime_config_t cfg;
	uint64_t machine;
	uint64_t budget;

	printk("runtime: target=zephyr\n");

#if defined(CONFIG_NET_L2_ETHERNET) && !defined(CONFIG_NET_CONFIG_AUTO_INIT)
	/* qemu: apply NET_CONFIG_* to e1000, not loopback (WASI still uses lo). */
	{
		struct net_if *eth = net_if_get_first_by_type(&NET_L2_GET_NAME(ETHERNET));
		int nrc;

		if (eth) {
			net_if_set_default(eth);
			nrc = net_config_init_by_iface(eth, "pm-metal", NET_CONFIG_NEED_IPV4,
							 CONFIG_NET_CONFIG_INIT_TIMEOUT * MSEC_PER_SEC);
			if (nrc != 0) {
				printk("runtime: eth net_config rc=%d\n", nrc);
			}
		} else {
			printk("runtime: eth iface missing\n");
		}
	}
#endif

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
	if (budget == 0) {
		cfg.bytecode_bytes = 0;
		cfg.memory_bytes = 0;
	} else {
		cfg.bytecode_bytes = PM_METAL_MEMORY_BYTECODE_BYTES;
		if (cfg.bytecode_bytes + (2ull * 1024ull * 1024ull) > budget) {
			cfg.bytecode_bytes = budget / 4;
			if (cfg.bytecode_bytes < (512ull * 1024ull)) {
				cfg.bytecode_bytes = budget / 2;
			}
		}
		if (cfg.bytecode_bytes > budget) {
			cfg.bytecode_bytes = budget;
		}
		cfg.memory_bytes = budget - cfg.bytecode_bytes;
		if (cfg.memory_bytes > PM_METAL_MEMORY_KHEAP_BYTES) {
			cfg.memory_bytes = PM_METAL_MEMORY_KHEAP_BYTES;
		}
	}
	cfg.vfs_root = PM_METAL_ZEPHYR_VFS_ROOT;
	{
		static const char *addr_pool[] = {
			"127.0.0.1/32",
			"::1/128",
		};
		static const char *ns_lookup_pool[] = {
			"localhost",
			"ip6-localhost",
		};

		cfg.addr_pool = addr_pool;
		cfg.addr_pool_count = 2;
		cfg.ns_lookup_pool = ns_lookup_pool;
		cfg.ns_lookup_pool_count = 2;
	}

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
	/* verify owns process/runtime teardown — do not shutdown again here. */
	if (pm_metal_zephyr_verify() != 0) {
		return 1;
	}
	return 0;
#else
	pm_metal_process_shutdown();
	pm_metal_runtime_shutdown();
	return 0;
#endif
}
