/*
 * T13 — reads "/scratch/hello.txt" back, from a separate process than the
 * one that wrote it (t12_tmpfs_write) — same tmpfs mount, proving the
 * backing actually persists across processes within one boot (see
 * scripts/verify-linux-tmpfs.sh). wasm32-wasip1.
 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	char buf[64];
	int fd = open("/scratch/hello.txt", O_RDONLY);

	if (fd < 0) {
		printf("t13_tmpfs_read: open failed\n");
		return 1;
	}

	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	close(fd);

	if (n < 0) {
		printf("t13_tmpfs_read: read failed\n");
		return 1;
	}

	buf[n] = '\0';
	printf("t13_tmpfs_read: %s", buf);
	return 0;
}
