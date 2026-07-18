/*
 * T25 — WASI preview1 socket extension, UDP client half. Sends to
 * mods/t24_udp_server's 127.0.0.1:9933 with a bounded sendto/recvfrom retry
 * loop (same race-coping idea as mods/t11_socket_client's connect retry).
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#ifdef __wasi__
#include <wasi_socket_ext.h>
#endif

int main(void)
{
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(9933);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	const char *msg = "hello udp";
	size_t msg_len = strlen(msg);
	char buf[128];
	int attempt;

	for (attempt = 0; attempt < 100; attempt++) {
		int fd = socket(AF_INET, SOCK_DGRAM, 0);
		struct timeval tv;
		struct sockaddr_in local;
		struct sockaddr_in peer;
		socklen_t peer_len;
		ssize_t n;

		if (fd < 0) {
			printf("t25_udp_client: socket() failed\n");
			return 1;
		}

		/* Bound ephemeral local port so peer replies have a stable dest;
		 * short RCVTIMEO so a lost first datagram cannot hang forever
		 * (Zephyr loopback + two wasm workers). */
		memset(&local, 0, sizeof(local));
		local.sin_family = AF_INET;
		local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		local.sin_port = 0;
		if (bind(fd, (struct sockaddr *)&local, sizeof(local)) != 0) {
			close(fd);
			continue;
		}
		tv.tv_sec = 0;
		tv.tv_usec = 100000; /* 100ms */
		(void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

		if (sendto(fd, msg, msg_len, 0, (struct sockaddr *)&addr, sizeof(addr))
		    < 0) {
			close(fd);
			struct timespec delay = { .tv_sec = 0, .tv_nsec = 20L * 1000 * 1000 };
			nanosleep(&delay, NULL);
			continue;
		}

		peer_len = sizeof(peer);
		n = recvfrom(fd, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&peer,
			     &peer_len);
		close(fd);
		if (n > 0) {
			buf[n] = '\0';
			if (strstr(buf, "echo: hello udp") != NULL) {
				printf("t25_udp_client: got: %s\n", buf);
				fflush(stdout);
				return 0;
			}
		}

		struct timespec delay = { .tv_sec = 0, .tv_nsec = 20L * 1000 * 1000 };
		nanosleep(&delay, NULL);
	}

	printf("t25_udp_client: sendto/recvfrom never succeeded\n");
	return 1;
}
