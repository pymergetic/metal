#include <pymergetic/metal/sys/sys.h>
#include <pymergetic/metal/sys/hostinfo.h>

#include <string.h>

static pm_metal_sys_bootstrap_t g_blob;
static int g_ready;

int pm_metal_sys_bootstrap_validate(const pm_metal_sys_bootstrap_t *blob)
{
	if (blob == NULL) {
		return -1;
	}
	if (!pm_metal_util_bigtag_valid(&blob->tag, PM_METAL_SYS_MAGIC, PM_METAL_SYS_VERSION)) {
		return -1;
	}
	if (blob->size != PM_METAL_SYS_BLOB_SIZE) {
		return -1;
	}
	return 0;
}

int pm_metal_sys_bootstrap_bind(const pm_metal_sys_bootstrap_t *blob)
{
	if (blob == NULL) {
		return -1;
	}
	if (pm_metal_sys_bootstrap_validate(blob) != 0) {
		return -1;
	}

	memcpy(&g_blob, blob, sizeof(g_blob));
	g_ready = 1;
	return 0;
}

int pm_metal_sys_init(void)
{
	if (g_ready) {
		return 0;
	}
#if defined(PM_METAL_BUILD_KERNEL)
	return -1;
#else
	if (pm_metal_sys_hostinfo_load(&g_blob) != 0) {
		return -1;
	}

	g_ready = 1;
	return 0;
#endif
}

int pm_metal_sys_ready(void)
{
	return g_ready;
}

uint64_t pm_metal_sys_built(void)
{
	return g_ready ? g_blob.tag.versiontag.built : 0;
}

uint64_t pm_metal_sys_machine_ram(void)
{
	return g_ready ? g_blob.machine_ram : 0;
}

uint64_t pm_metal_sys_arena_budget(void)
{
	return g_ready ? g_blob.arena_budget : 0;
}

uint64_t pm_metal_sys_link_used(void)
{
	return g_ready ? g_blob.link_used : 0;
}
