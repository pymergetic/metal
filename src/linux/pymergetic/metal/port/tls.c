/*
 * Port TLS — linux bind (system CA bundle path).
 */
#include "pymergetic/metal/port/tls.h"

#include "pymergetic/metal/port/platform.h"

#include <string.h>

int pm_metal_port_tls_ca_file(char *out, size_t cap)
{
	static const char *const candidates[] = {
		"/etc/ssl/certs/ca-certificates.crt", /* Debian/Ubuntu */
		"/etc/pki/tls/certs/ca-bundle.crt", /* Fedora/RHEL */
		"/etc/ssl/cert.pem", /* some LibreSSL / Alpine layouts */
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
