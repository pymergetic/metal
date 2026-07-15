/*
 * T16 — reads "/scratch/hello.txt" which is expected to exist only because
 * boot-time populate_all() extracted an embedded ustar after Stage B (see
 * scripts/verify-linux-populate.sh, docs/MOUNT.md Phase 4). wasm32-wasip1.
 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	char buf[64];
	int fd = open("/scratch/hello.txt", O_RDONLY);

	if (fd < 0) {
		printf("t16_populate_read: open failed\n");
		return 1;
	}

	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	close(fd);

	if (n < 0) {
		printf("t16_populate_read: read failed\n");
		return 1;
	}

	buf[n] = '\0';
	printf("t16_populate_read: %s", buf);
	return 0;
}
