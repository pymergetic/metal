/*
 * T24 — WASI preview1 socket extension, UDP echo server half. Binds
 * 127.0.0.1:9933, recvfrom once, sendto reply "echo: "+payload. Paired with
 * mods/t25_udp_client. Same SOCKET marker / wasi_socket_ext.h wiring as
 * mods/t10_socket_server; cfg->addr_pool must allow 127.0.0.1/32.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#ifdef __wasi__
#include <wasi_socket_ext.h>
#endif

int main(void)
{
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		printf("t24_udp_server: socket() failed\n");
		return 1;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(9933);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		printf("t24_udp_server: bind() failed\n");
		close(fd);
		return 1;
	}

	{
		struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };

		(void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	}

	printf("t24_udp_server: listening\n");
	fflush(stdout);

	char buf[128];
	struct sockaddr_in peer;
	socklen_t peer_len = sizeof(peer);
	ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&peer,
			     &peer_len);
	if (n <= 0) {
		printf("t24_udp_server: recvfrom() failed\n");
		close(fd);
		return 1;
	}
	buf[n] = '\0';

	char reply[160];
	int reply_len = snprintf(reply, sizeof(reply), "echo: %s", buf);
	if (reply_len < 0) {
		printf("t24_udp_server: snprintf() failed\n");
		close(fd);
		return 1;
	}
	if ((size_t)reply_len >= sizeof(reply)) {
		reply_len = (int)sizeof(reply) - 1;
	}
	if (sendto(fd, reply, (size_t)reply_len, 0, (struct sockaddr *)&peer, peer_len)
	    < 0) {
		printf("t24_udp_server: sendto() failed\n");
		close(fd);
		return 1;
	}

	printf("t24_udp_server: served \"%s\"\n", buf);
	fflush(stdout);

	close(fd);
	return 0;
}
