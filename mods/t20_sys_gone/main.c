/*
 * T20 — after t19 umounted /dyn, open must fail (mount gone for this new
 * process). Exit 0 when open fails; exit 1 if still mounted.
 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	int fd = open("/dyn/hello.txt", O_RDONLY);

	if (fd < 0) {
		printf("t20_sys_gone: open failed\n");
		return 0;
	}
	close(fd);
	printf("t20_sys_gone: unexpectedly still mounted\n");
	return 1;
}
