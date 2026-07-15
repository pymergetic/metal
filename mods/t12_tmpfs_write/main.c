/*
 * T12 — writes "hello from tmpfs\n" to "/scratch/hello.txt", proving guest
 * WASI I/O can write through a tmpfs mount (see mount/tmpfs.h,
 * scripts/verify-linux-tmpfs.sh). Paired with t13_tmpfs_read, which reads
 * this same file back from a *separate* process — one mod per side of the
 * "write from one, read from another" proof, matching t6/t7's own
 * pipe-writer/reader split. wasm32-wasip1.
 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	static const char msg[] = "hello from tmpfs\n";
	int fd = open("/scratch/hello.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);

	if (fd < 0) {
		printf("t12_tmpfs_write: open failed\n");
		return 1;
	}

	ssize_t n = write(fd, msg, sizeof(msg) - 1);
	close(fd);

	if (n != (ssize_t)sizeof(msg) - 1) {
		printf("t12_tmpfs_write: write failed\n");
		return 1;
	}

	printf("t12_tmpfs_write: wrote %zd bytes\n", n);
	return 0;
}
