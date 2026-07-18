/*
 * T1 — read a file from preopened /. wasm32-wasip1.
 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	char buf[64];
	int fd = open("/README", O_RDONLY);

	if (fd < 0) {
		printf("t1_read: open failed\n");
		return 1;
	}

	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	close(fd);

	if (n < 0) {
		printf("t1_read: read failed\n");
		return 1;
	}

	buf[n] = '\0';
	printf("t1_read: %s", buf);
	return 0;
}
