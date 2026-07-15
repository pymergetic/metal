/*
 * T18 — write + read /dyn/hello.txt after t17 mounted that tmpfs in a prior
 * process (same boot). wasm32-wasip1, no MOUNT marker needed for plain I/O.
 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	static const char msg[] = "hello from guest mount\n";
	char buf[64];
	int fd = open("/dyn/hello.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);

	if (fd < 0) {
		printf("t18_sys_use: open write failed\n");
		return 1;
	}
	if (write(fd, msg, sizeof(msg) - 1) != (ssize_t)(sizeof(msg) - 1)) {
		close(fd);
		printf("t18_sys_use: write failed\n");
		return 1;
	}
	close(fd);

	fd = open("/dyn/hello.txt", O_RDONLY);
	if (fd < 0) {
		printf("t18_sys_use: open read failed\n");
		return 1;
	}
	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n < 0) {
		printf("t18_sys_use: read failed\n");
		return 1;
	}
	buf[n] = '\0';
	printf("t18_sys_use: %s", buf);
	return 0;
}
