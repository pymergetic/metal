/*
 * T15 — reads "/other/hello.txt", a *differently-named* tmpfs source
 * mounted alongside "/scratch" in the same boot (see
 * scripts/verify-linux-tmpfs.sh's fstab). Expected to fail (nothing was
 * ever written here) — proving two independently-named tmpfs sources
 * really are independent backings, not silently the same one (see
 * docs/MOUNT.md "Named ramdisks"). A non-zero exit here is the pass case;
 * the verify script checks for that explicitly. wasm32-wasip1.
 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	int fd = open("/other/hello.txt", O_RDONLY);

	if (fd < 0) {
		printf("t15_tmpfs_read_other: open failed (expected)\n");
		return 1;
	}

	close(fd);
	printf("t15_tmpfs_read_other: unexpectedly found a file\n");
	return 1;
}
