/*
 * T17 — privileged guest mount() of a named tmpfs at /dyn, then use it in
 * the *same* process (live remount — see docs/MOUNT.md).
 * Needs mods/t17_sys_mount/MOUNT + -DPM_METAL_BUILD_KERNEL.
 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "pymergetic/metal/mount/mount.h"

int main(void)
{
	static const char msg[] = "hello from same-process mount\n";
	char buf[64];
	int fd;
	ssize_t n;

	if (mount("dyn", "/dyn", "tmpfs", 0, NULL) != 0) {
		printf("t17_sys_mount: mount failed\n");
		return 1;
	}

	fd = open("/dyn/hello.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		printf("t17_sys_mount: open write failed\n");
		return 1;
	}
	if (write(fd, msg, sizeof(msg) - 1) != (ssize_t)(sizeof(msg) - 1)) {
		close(fd);
		printf("t17_sys_mount: write failed\n");
		return 1;
	}
	close(fd);

	fd = open("/dyn/hello.txt", O_RDONLY);
	if (fd < 0) {
		printf("t17_sys_mount: open read failed\n");
		return 1;
	}
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n < 0) {
		printf("t17_sys_mount: read failed\n");
		return 1;
	}
	buf[n] = '\0';
	printf("t17_sys_mount: %s", buf);
	return 0;
}
