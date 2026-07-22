/*
 * T11 — WASI preview1 socket extension, client half. Connects to
 * mods/t10_socket_server's own fixed 127.0.0.1:9931, exactly the plain
 * BSD socket()/connect() calls an ordinary POSIX client would use (see
 * that mod's own header on how those actually resolve — same
 * wasi_socket_ext.h/.c pair, this directory's own SOCKET marker file).
 * Both mods are spawned as separate processes (runtime/process.h) with
 * no direct handoff between them — this file's own retry loop (bounded,
 * not a busy-spin) is what actually copes with the real race of possibly
 * starting before t10 has reached listen() yet, rather than assuming
 * some fixed startup order between two independently scheduled worker
 * threads (see tests/process_test.c's own spawn() calls for both).
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#ifdef __wasi__
#include <wasi_socket_ext.h>
#endif

static void
retry_pause(void)
{
#if defined(__wasi__)
	int i;

	for (i = 0; i < 64; i++)
		sched_yield();
#else
	struct timespec delay = { .tv_sec = 0, .tv_nsec = 20L * 1000 * 1000 };

	nanosleep(&delay, NULL);
#endif
}

int main(void)
{
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(9931);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	int fd = -1;
	int attempt;

	for (attempt = 0; attempt < 100; attempt++) {
		fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0) {
			printf("t11_socket_client: socket() failed\n");
			return 1;
		}
		if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
			break;
		}
		close(fd);
		fd = -1;

		retry_pause();
	}

	if (fd < 0) {
		printf("t11_socket_client: connect() never succeeded\n");
		return 1;
	}

	const char *msg = "hello socket";
	size_t msg_len = strlen(msg);
	size_t sent = 0;

	while (sent < msg_len) {
		ssize_t w = write(fd, msg + sent, msg_len - sent);
		if (w <= 0) {
			printf("t11_socket_client: write() failed\n");
			close(fd);
			return 1;
		}
		sent += (size_t)w;
	}

	char buf[128];
	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	close(fd);

	if (n <= 0) {
		printf("t11_socket_client: read() failed\n");
		return 1;
	}
	buf[n] = '\0';

	printf("t11_socket_client: got: %s\n", buf);
	fflush(stdout);
	return 0;
}
