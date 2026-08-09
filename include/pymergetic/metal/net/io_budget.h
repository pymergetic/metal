#ifndef PM_METAL_NET_IO_BUDGET_H_
#define PM_METAL_NET_IO_BUDGET_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Wire chunk for TLS / HTTP-client / py-recv. */
#ifndef PM_METAL_IO_WIRE_MAX
#define PM_METAL_IO_WIRE_MAX (32u * 1024u)
#endif

/* ASGI server iobuf ceiling. */
#ifndef PM_METAL_ASGI_IO_MAX
#define PM_METAL_ASGI_IO_MAX (4u * 1024u * 1024u)
#endif

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_NET_IO_BUDGET_H_ */
