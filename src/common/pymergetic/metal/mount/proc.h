/*
 * Proc fstype sentinel — not a real host directory. map_dir_list emits
 * "/proc::<sentinel>"; Metal's WASI os_* wraps accept it and answer
 * opens from registered hooks. See docs/MOUNT.md "Procfs (virtual)".
 */
#ifndef PYMERGETIC_METAL_MOUNT_PROC_H_
#define PYMERGETIC_METAL_MOUNT_PROC_H_

#include <stddef.h>

#include "pymergetic/metal/mount/ops.h"

#define PM_METAL_MOUNT_PROC_NAME_MAX 64
#define PM_METAL_MOUNT_PROC_MAX_HOOKS 16
#define PM_METAL_MOUNT_PROC_CONTENT_MAX (16u * 1024u)

/* Host-side token in map_dir_list / establish() — never a filesystem path. */
#define PM_METAL_MOUNT_PROC_SENTINEL "pm-metal:proc"

typedef int (*pm_metal_mount_proc_hook_fn)(char *out, size_t cap, size_t *out_len);

/* impl: common — src/common/pymergetic/metal/mount/proc.c */
int pm_metal_mount_proc_register(const char *name, pm_metal_mount_proc_hook_fn fn);

/* Stamp runtime boot for /proc/uptime. Call once from runtime_init().
 * impl: common — src/common/pymergetic/metal/mount/proc/boot.c */
void pm_metal_mount_proc_note_boot(void);

/*
 * Bind this thread's current guest argv/env for /proc/self/{cmdline,environ}.
 * Call at the start of run_ex(); unbind when run_ex() returns.
 * impl: common — src/common/pymergetic/metal/mount/proc/guest.c
 */
void pm_metal_mount_proc_bind_current(int argc, char **argv, int envc, const char **envp);
void pm_metal_mount_proc_unbind_current(void);

/* impl: common — src/common/pymergetic/metal/mount/proc.c */
int pm_metal_mount_proc_is_sentinel(const char *host_path);

/* Look up a root-relative hook name ("mounts"). 0/-1.
 * impl: common — src/common/pymergetic/metal/mount/proc.c */
int pm_metal_mount_proc_lookup(const char *name, pm_metal_mount_proc_hook_fn *out_fn);

/* Enumerate registered root hooks (not including self/).
 * impl: common — src/common/pymergetic/metal/mount/proc.c */
int pm_metal_mount_proc_hook_count(void);
const char *pm_metal_mount_proc_hook_name(int index);

/* Generate self/cmdline or self/environ from the bound current guest.
 * impl: common — src/common/pymergetic/metal/mount/proc/cmdline.c
 *                src/common/pymergetic/metal/mount/proc/environ.c */
int pm_metal_mount_proc_generate_cmdline(char *out, size_t cap, size_t *out_len);
int pm_metal_mount_proc_generate_environ(char *out, size_t cap, size_t *out_len);

/* impl: common — src/common/pymergetic/metal/mount/proc.c */
const pm_metal_mount_ops_t *pm_metal_mount_proc_ops(void);

#endif /* PYMERGETIC_METAL_MOUNT_PROC_H_ */
