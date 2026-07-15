#include "pymergetic/metal/mount/proc/cmdline.h"

#include "pymergetic/metal/mount/proc/guest.h"
#include "pymergetic/metal/mount/proc/util.h"

int pm_metal_mount_proc_generate_cmdline(char *out, size_t cap, size_t *out_len)
{
	return pm_metal_mount_proc_put_nul_list(out, cap, out_len, pm_metal_mount_proc_guest_argc(),
						(const char *const *)pm_metal_mount_proc_guest_argv());
}
