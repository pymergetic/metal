#include <pymergetic/metal/runtime/entry.h>
#include <pymergetic/metal/sys/sys.h>

#include <stdio.h>

int main(int argc, char **argv)
{
	const char *vfs_root;
	char handoff[256];
	pm_metal_runtime_config_t cfg;

	vfs_root = (argc >= 2) ? argv[1] : "build/linux/vfs";

	if (snprintf(handoff, sizeof(handoff), "%s%s", vfs_root, PM_METAL_SYS_HANDOFF_VFS_ROOT) >=
	    (int)sizeof(handoff)) {
		fprintf(stderr, "handoff path too long\n");
		return 1;
	}

	cfg.target = "linux";
	cfg.handoff_vfs_root = handoff;

	return pm_metal_runtime_main(&cfg) == 0 ? 0 : 1;
}
