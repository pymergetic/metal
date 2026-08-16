#ifndef PM_METAL_FW_ERRNO_H
#define PM_METAL_FW_ERRNO_H

#define ENOENT 2
#define ENOMEM 12
#define EINVAL 22
#define EIO 5

#ifndef errno
#define errno (*pm_metal_errno_loc())
int *pm_metal_errno_loc(void);
#endif

#endif
