/*
 * T28 — WASI preview1 socket extension getaddrinfo("localhost") via
 * wasi_socket_ext.h (POSIX getaddrinfo wrapping sock_addr_resolve). On
 * Zephyr, os_socket_addr_resolve short-circuits localhost → 127.0.0.1 so
 * this works offline; ns_lookup_pool must allow "localhost".
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#ifdef __wasi__
#include <wasi_socket_ext.h>
#endif

int main(void)
{
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	int rc;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	rc = getaddrinfo("localhost", NULL, &hints, &res);
	if (rc != 0 || res == NULL || res->ai_addr == NULL) {
		printf("t28_dns_lookup: getaddrinfo(localhost) failed (%d)\n", rc);
		if (res != NULL) {
			freeaddrinfo(res);
		}
		return 1;
	}

	if (res->ai_family != AF_INET) {
		printf("t28_dns_lookup: expected AF_INET result\n");
		freeaddrinfo(res);
		return 1;
	}

	struct sockaddr_in *sin = (struct sockaddr_in *)res->ai_addr;
	unsigned char *b = (unsigned char *)&sin->sin_addr.s_addr;

	printf("t28_dns_lookup: localhost -> %u.%u.%u.%u\n",
	       (unsigned)b[0], (unsigned)b[1], (unsigned)b[2], (unsigned)b[3]);
	fflush(stdout);

	freeaddrinfo(res);
	return 0;
}
