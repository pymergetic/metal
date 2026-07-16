/*
 * T15 — reads "/other/hello.txt", a *differently-named* tmpfs source
 * mounted alongside "/scratch" in the same boot (see
 * scripts/verify-linux-tmpfs.sh's fstab). Expected to fail (nothing was
 * ever written here) — proving two independently-named tmpfs sources
 * really are independent backings, not silently the same one (see
 * docs/MOUNT.md "Named ramdisks"). Exit 0 when open fails (correct
 * isolation); exit 1 if a file is found.
 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	int fd = open("/other/hello.txt", O_RDONLY);

	if (fd < 0) {
		printf("t15_tmpfs_read_other: open failed (expected)\n");
		return 0;
	}

	close(fd);
	printf("t15_tmpfs_read_other: unexpectedly found a file\n");
	return 1;
}
