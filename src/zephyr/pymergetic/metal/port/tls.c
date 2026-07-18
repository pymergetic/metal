/*
 * Port TLS — zephyr bind. native_sim: host CA path for host-linked curl
 * (not visible via Zephyr fs_stat). qemu mbedtls uses embedded roots when
 * this returns -1.
 */
#include "pymergetic/metal/port/tls.h"

#include <string.h>

#if defined(CONFIG_ARCH_POSIX)

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

#else /* !CONFIG_ARCH_POSIX */

#include "pymergetic/metal/port/platform.h"

int pm_metal_port_tls_ca_file(char *out, size_t cap)
{
	static const char *const candidates[] = {
		"/etc/ssl/certs/ca-certificates.crt",
		"/etc/pki/tls/certs/ca-bundle.crt",
		"/etc/ssl/cert.pem",
	};
	size_t i;

	if (!out || cap == 0) {
		return -1;
	}
	for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
		if (pm_metal_port_file_exists(candidates[i])) {
			size_t n = strlen(candidates[i]);

			if (n + 1 > cap) {
				return -1;
			}
			memcpy(out, candidates[i], n + 1);
			return 0;
		}
	}
	return -1;
}

#endif
