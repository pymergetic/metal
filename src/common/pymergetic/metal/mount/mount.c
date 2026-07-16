/*
 * Privileged mount()/umount() — host impl + wasi native bridge.
 * See include/pymergetic/metal/mount/mount.h (same shape as util/size.c).
 */
#include "pymergetic/metal/mount/mount.h"

#include "pymergetic/metal/mount/fstab.h"
#include "pymergetic/metal/mount/ops.h"
#include "pymergetic/metal/mount/table.h"
#include "pymergetic/metal/runtime/runtime.h"

#include <stdint.h>
#include <string.h>

#include "wasm_export.h"

int pm_metal_mount_mount(const char *source, const char *target, const char *fstype, const char *options)
{
	return pm_metal_mount_fstab_apply_fields(source, target, fstype, options);
}

int pm_metal_mount_umount(const char *target)
{
	return pm_metal_umount(target);
}

int pm_metal_mount_fstype_count(void)
{
	return (int)PM_METAL_MOUNT_KIND_COUNT;
}

int pm_metal_mount_fstype_name(unsigned index, char *out, size_t cap)
{
	const char *name;
	size_t len;

	if (!out || cap == 0) {
		return -1;
	}
	name = pm_metal_mount_kind_name((pm_metal_mount_kind_t)index);
	if (!name) {
		return -1;
	}
	len = strlen(name);
	if (len + 1 > cap) {
		return -1;
	}
	memcpy(out, name, len + 1);
	return 0;
}

static int32_t pm_metal_mount_mount_native(wasm_exec_env_t exec_env, char *source, char *target, char *fstype,
					    char *options)
{
	(void)exec_env;
	if (!pm_metal_runtime_allow_guest_mount()) {
		return -1;
	}
	return (int32_t)pm_metal_mount_mount(source, target, fstype, options && options[0] ? options : NULL);
}

static int32_t pm_metal_mount_umount_native(wasm_exec_env_t exec_env, char *target)
{
	(void)exec_env;
	if (!pm_metal_runtime_allow_guest_mount()) {
		return -1;
	}
	return (int32_t)pm_metal_mount_umount(target);
}

static int32_t pm_metal_mount_fstype_count_native(wasm_exec_env_t exec_env)
{
	(void)exec_env;
	return (int32_t)pm_metal_mount_fstype_count();
}

static int32_t pm_metal_mount_fstype_name_native(wasm_exec_env_t exec_env, uint32_t index, char *out,
						 uint32_t cap)
{
	(void)exec_env;
	return (int32_t)pm_metal_mount_fstype_name((unsigned)index, out, (size_t)cap);
}

static NativeSymbol g_pm_metal_mount_native_symbols[] = {
	{"pm_metal_mount_mount", (void *)pm_metal_mount_mount_native, "($$$$)i", NULL},
	{"pm_metal_mount_umount", (void *)pm_metal_mount_umount_native, "($)i", NULL},
	{"pm_metal_mount_fstype_count", (void *)pm_metal_mount_fstype_count_native, "()i", NULL},
	{"pm_metal_mount_fstype_name", (void *)pm_metal_mount_fstype_name_native, "(i*~)i", NULL},
};

int pm_metal_mount_native_register(void)
{
	if (!wasm_runtime_register_natives(PM_METAL_MOUNT_WASI_MODULE, g_pm_metal_mount_native_symbols,
					    sizeof(g_pm_metal_mount_native_symbols)
						    / sizeof(g_pm_metal_mount_native_symbols[0]))) {
		return -1;
	}
	return 0;
}
