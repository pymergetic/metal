#include "pymergetic/metal/mount/proc/guest.h"

#include "pymergetic/metal/mount/proc.h"

typedef struct pm_metal_mount_proc_guest_tls {
	int argc;
	char **argv;
	int envc;
	const char **envp;
} pm_metal_mount_proc_guest_tls_t;

static __thread pm_metal_mount_proc_guest_tls_t g_pm_metal_mount_proc_guest;

void pm_metal_mount_proc_bind_current(int argc, char **argv, int envc, const char **envp)
{
	g_pm_metal_mount_proc_guest.argc = (argc > 0 && argv) ? argc : 0;
	g_pm_metal_mount_proc_guest.argv = (argc > 0 && argv) ? argv : NULL;
	g_pm_metal_mount_proc_guest.envc = (envc > 0 && envp) ? envc : 0;
	g_pm_metal_mount_proc_guest.envp = (envc > 0 && envp) ? envp : NULL;
}

void pm_metal_mount_proc_unbind_current(void)
{
	g_pm_metal_mount_proc_guest.argc = 0;
	g_pm_metal_mount_proc_guest.argv = NULL;
	g_pm_metal_mount_proc_guest.envc = 0;
	g_pm_metal_mount_proc_guest.envp = NULL;
}

int pm_metal_mount_proc_guest_argc(void)
{
	return g_pm_metal_mount_proc_guest.argc;
}

char **pm_metal_mount_proc_guest_argv(void)
{
	return g_pm_metal_mount_proc_guest.argv;
}

int pm_metal_mount_proc_guest_envc(void)
{
	return g_pm_metal_mount_proc_guest.envc;
}

const char **pm_metal_mount_proc_guest_envp(void)
{
	return g_pm_metal_mount_proc_guest.envp;
}
