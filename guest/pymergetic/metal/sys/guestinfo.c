#include <pymergetic/metal/sys/guestinfo.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

int pm_metal_sys_guestinfo_load(pm_metal_sys_bootstrap_t *out)
{
	int fd;
	ssize_t n;

	if (out == NULL) {
		return -1;
	}

	fd = open(PM_METAL_SYS_BOOTSTRAP_PATH, O_RDONLY);
	if (fd < 0) {
		return -1;
	}

	memset(out, 0, sizeof(*out));
	n = read(fd, out, sizeof(*out));
	close(fd);

	if (n != (ssize_t)sizeof(*out)) {
		return -1;
	}
	if (pm_metal_sys_bootstrap_validate(out) != 0) {
		return -1;
	}

	return 0;
}
