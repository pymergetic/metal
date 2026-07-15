/*
 * T14 — reads "/scratchB/hello.txt" (a *second* fstab line naming the same
 * tmpfs source as "/scratch", see scripts/verify-linux-tmpfs.sh's fstab) —
 * seeing t12_tmpfs_write's content here too proves a repeated fstab name
 * reuses the already-established backing rather than re-creating a second,
 * independent one (see docs/MOUNT.md "Named ramdisks"). wasm32-wasip1.
 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	char buf[64];
	int fd = open("/scratchB/hello.txt", O_RDONLY);

	if (fd < 0) {
		printf("t14_tmpfs_read_alt: open failed\n");
		return 1;
	}

	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	close(fd);

	if (n < 0) {
		printf("t14_tmpfs_read_alt: read failed\n");
		return 1;
	}

	buf[n] = '\0';
	printf("t14_tmpfs_read_alt: %s", buf);
	return 0;
}
