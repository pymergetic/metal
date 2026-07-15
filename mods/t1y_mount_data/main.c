/*
 * T1y — like t1_read, but opens "/data/README" instead of "/README" —
 * exercises a *non-root* mount table entry from guest WASI I/O (see
 * scripts/verify-linux-mount.sh, mount/table.h's own longest-prefix
 * resolve). wasm32-wasip1.
 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	char buf[64];
	int fd = open("/data/README", O_RDONLY);

	if (fd < 0) {
		printf("t1y_mount_data: open failed\n");
		return 1;
	}

	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	close(fd);

	if (n < 0) {
		printf("t1y_mount_data: read failed\n");
		return 1;
	}

	buf[n] = '\0';
	printf("t1y_mount_data: %s", buf);
	return 0;
}
