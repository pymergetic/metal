#include "pymergetic/metal/mount/proc/boot.h"

#include "pymergetic/metal/mount/proc.h"
#include "pymergetic/metal/port/platform.h"

static uint64_t g_pm_metal_mount_proc_boot_ms;
static int g_pm_metal_mount_proc_boot_set;

void pm_metal_mount_proc_note_boot(void)
{
	g_pm_metal_mount_proc_boot_ms = pm_metal_port_monotonic_ms();
	g_pm_metal_mount_proc_boot_set = 1;
}

int pm_metal_mount_proc_boot_is_set(void)
{
	return g_pm_metal_mount_proc_boot_set;
}

uint64_t pm_metal_mount_proc_boot_ms(void)
{
	return g_pm_metal_mount_proc_boot_ms;
}
