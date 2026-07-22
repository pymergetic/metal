/*
 * T27 — WASI preview1 socket extension, IPv6 TCP client. Connects to
 * mods/t26_ipv6_server's ::1:9932 with a bounded connect() retry loop
 * (same race-coping idea as mods/t11_socket_client).
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
	struct sockaddr_in6 addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(9932);
	addr.sin6_addr = in6addr_loopback;

	int fd = -1;
	int attempt;

	for (attempt = 0; attempt < 100; attempt++) {
		fd = socket(AF_INET6, SOCK_STREAM, 0);
		if (fd < 0) {
			printf("t27_ipv6_client: socket() failed\n");
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
		printf("t27_ipv6_client: connect() never succeeded\n");
		return 1;
	}

	const char *msg = "hello ipv6";
	size_t msg_len = strlen(msg);
	size_t sent = 0;

	while (sent < msg_len) {
		ssize_t w = write(fd, msg + sent, msg_len - sent);
		if (w <= 0) {
			printf("t27_ipv6_client: write() failed\n");
			close(fd);
			return 1;
		}
		sent += (size_t)w;
	}

	char buf[128];
	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	close(fd);

	if (n <= 0) {
		printf("t27_ipv6_client: read() failed\n");
		return 1;
	}
	buf[n] = '\0';

	printf("t27_ipv6_client: got: %s\n", buf);
	fflush(stdout);
	return 0;
}
