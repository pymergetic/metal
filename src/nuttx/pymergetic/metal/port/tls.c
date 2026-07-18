/*
 * Port TLS — nuttx bind. On sim, host-linked libcurl opens the host CA
 * bundle with host libc (not NuttX VFS).
 */
#include "pymergetic/metal/port/tls.h"

#include <string.h>

int pm_metal_port_tls_ca_file(char *out, size_t cap)
{
	static const char path[] = "/etc/ssl/certs/ca-certificates.crt";
	size_t n = sizeof(path) - 1;

	if (!out || cap == 0 || n + 1 > cap) {
		return -1;
	}
	memcpy(out, path, n + 1);
	return 0;
}
