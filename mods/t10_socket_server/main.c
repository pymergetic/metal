/*
 * T10 — WASI preview1's own socket extension (wasi1, not preview2's
 * sockets — see docs/RUNTIME.md "Sockets"), server half. Plain BSD
 * socket()/bind()/listen()/accept() calls, backed by WAMR's own
 * wasi_socket_ext.h/.c (see this directory's own empty SOCKET marker
 * file, scripts/build-mod.sh) which in turn calls the non-standard
 * wasi_snapshot_preview1 sock_* imports libc_wasi_wrapper.c implements —
 * no code here knows it's talking to a sandboxed address pool rather
 * than a real kernel socket. Paired with mods/t11_socket_client (that mod
 * connects here). Fixed 127.0.0.1:9931 — this codebase's own runtime.h's
 * own cfg->addr_pool must allow that exact address (see
 * src/linux/process_test.c's own cfg literal) or bind() itself fails
 * before a single byte of this file's own logic runs at all.
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
	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd < 0) {
		printf("t10_socket_server: socket() failed\n");
		return 1;
	}

	int reuse = 1;
	if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
		printf("t10_socket_server: setsockopt(SO_REUSEADDR) failed\n");
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(9931);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		printf("t10_socket_server: bind() failed\n");
		close(listen_fd);
		return 1;
	}
	if (listen(listen_fd, 1) != 0) {
		printf("t10_socket_server: listen() failed\n");
		close(listen_fd);
		return 1;
	}

	/* Printed (and flushed) only once the listening socket is actually
	 * up — mods/t11_socket_client's own retry loop is what actually
	 * copes with the real race (this line reaching the shared console
	 * doesn't guarantee t11 has started yet on its own separate
	 * process), but this still marks the earliest point a connection
	 * could succeed, for anyone reading the combined log by eye. */
	printf("t10_socket_server: listening\n");
	fflush(stdout);

	int conn_fd = accept(listen_fd, NULL, NULL);
	if (conn_fd < 0) {
		printf("t10_socket_server: accept() failed\n");
		close(listen_fd);
		return 1;
	}

	char buf[128];
	ssize_t n = read(conn_fd, buf, sizeof(buf) - 1);
	if (n <= 0) {
		printf("t10_socket_server: read() failed\n");
		close(conn_fd);
		close(listen_fd);
		return 1;
	}
	buf[n] = '\0';

	char reply[160];
	int reply_len = snprintf(reply, sizeof(reply), "echo: %s", buf);
	if (reply_len < 0) {
		printf("t10_socket_server: snprintf() failed\n");
		close(conn_fd);
		close(listen_fd);
		return 1;
	}
	if ((size_t)reply_len >= sizeof(reply)) {
		reply_len = (int)sizeof(reply) - 1;
	}
	write(conn_fd, reply, (size_t)reply_len);

	printf("t10_socket_server: served \"%s\"\n", buf);
	fflush(stdout);

	close(conn_fd);
	close(listen_fd);
	return 0;
}
