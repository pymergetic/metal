/*
 * Per-thread guest argv/env for /proc/self/{cmdline,environ}.
 */
#ifndef PYMERGETIC_METAL_MOUNT_PROC_GUEST_H_
#define PYMERGETIC_METAL_MOUNT_PROC_GUEST_H_

/* impl: common — src/common/pymergetic/metal/mount/proc/guest.c */
int pm_metal_mount_proc_guest_argc(void);
char **pm_metal_mount_proc_guest_argv(void);
int pm_metal_mount_proc_guest_envc(void);
const char **pm_metal_mount_proc_guest_envp(void);

#endif /* PYMERGETIC_METAL_MOUNT_PROC_GUEST_H_ */
