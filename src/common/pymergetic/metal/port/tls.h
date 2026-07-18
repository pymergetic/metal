/*
 * Port — TLS trust-store floor (OS bind). Callers use net/tls.
 */
#ifndef PYMERGETIC_METAL_PORT_TLS_H_
#define PYMERGETIC_METAL_PORT_TLS_H_

#include <stddef.h>

/*
 * Host path to a PEM/CRT CA bundle for peer verify (e.g. curl
 * CURLOPT_CAINFO). Writes NUL-terminated path into out/cap.
 * Returns 0, or -1 if this plat has no file-based store yet
 * (embedded PEM can land later under the same port/tls surface).
 * impl: bind — src/<plat>/pymergetic/metal/port/tls.c
 */
int pm_metal_port_tls_ca_file(char *out, size_t cap);

#endif /* PYMERGETIC_METAL_PORT_TLS_H_ */
