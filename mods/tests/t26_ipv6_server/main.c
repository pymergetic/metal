/*
 * T26 — WASI preview1 socket extension, IPv6 TCP echo server. Binds ::1
 * port 9932 (AF_INET6). Same echo pattern as mods/t10_socket_server; paired
 * with mods/t27_ipv6_client. cfg->addr_pool must allow ::1/128.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef __wasi__
#include <wasi_socket_ext.h>
#endif

int main(void)
{
	int listen_fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (listen_fd < 0) {
		printf("t26_ipv6_server: socket() failed\n");
		return 1;
	}

	int reuse = 1;
	if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
		printf("t26_ipv6_server: setsockopt(SO_REUSEADDR) failed\n");
	}

	struct sockaddr_in6 addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(9932);
	addr.sin6_addr = in6addr_loopback;

	if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		printf("t26_ipv6_server: bind() failed\n");
		close(listen_fd);
		return 1;
	}
	if (listen(listen_fd, 1) != 0) {
		printf("t26_ipv6_server: listen() failed\n");
		close(listen_fd);
		return 1;
	}

	printf("t26_ipv6_server: listening\n");
	fflush(stdout);

	int conn_fd = accept(listen_fd, NULL, NULL);
	if (conn_fd < 0) {
		printf("t26_ipv6_server: accept() failed\n");
		close(listen_fd);
		return 1;
	}

	char buf[128];
	ssize_t n = read(conn_fd, buf, sizeof(buf) - 1);
	if (n <= 0) {
		printf("t26_ipv6_server: read() failed\n");
		close(conn_fd);
		close(listen_fd);
		return 1;
	}
	buf[n] = '\0';

	char reply[160];
	int reply_len = snprintf(reply, sizeof(reply), "echo: %s", buf);
	if (reply_len < 0) {
		printf("t26_ipv6_server: snprintf() failed\n");
		close(conn_fd);
		close(listen_fd);
		return 1;
	}
	if ((size_t)reply_len >= sizeof(reply)) {
		reply_len = (int)sizeof(reply) - 1;
	}
	{
		size_t sent = 0;

		while (sent < (size_t)reply_len) {
			ssize_t n = write(conn_fd, reply + sent, (size_t)reply_len - sent);

			if (n <= 0) {
				perror("t26_ipv6_server: write");
				close(conn_fd);
				close(listen_fd);
				return 1;
			}
			sent += (size_t)n;
		}
	}

	printf("t26_ipv6_server: served \"%s\"\n", buf);
	fflush(stdout);

	close(conn_fd);
	close(listen_fd);
	return 0;
}
