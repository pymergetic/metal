/*
 * Port TLS — zephyr bind. File CA store not wired yet; embed PEM later.
 */
#include "pymergetic/metal/port/tls.h"

int pm_metal_port_tls_ca_file(char *out, size_t cap)
{
	(void)out;
	(void)cap;
	return -1;
}
