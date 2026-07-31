/** @file
  Compile-time mem/buffer limit rows for net wire, ASGI, TLS, and µPy.
**/
#include <pymergetic/metal/runtime/mem/limit.h>

#include <pymergetic/metal/net/io_budget.h>
#include <pymergetic/metal/net/tls/tls.h>
#include <pymergetic/metal/py/py.h>

#include "../../net/asgi/asgi_internal.h"

PM_METAL_MEM_LIMIT(g_pm_metal_lim_net_IO_WIRE_MAX,
                   "net",
                   "PM_METAL_IO_WIRE_MAX",
                   PM_METAL_IO_WIRE_MAX,
                   "bytes",
                   "TLS/HTTP-client/py-recv wire chunk");

PM_METAL_MEM_LIMIT(g_pm_metal_lim_net_asgi_ASGI_IO_MAX,
                   "net.asgi",
                   "ASGI_IO_MAX",
                   ASGI_IO_MAX,
                   "bytes",
                   "ASGI server/conn iobuf (pm_metal_mem_map)");

PM_METAL_MEM_LIMIT(g_pm_metal_lim_net_asgi_ASGI_MOUNT_MAX,
                   "net.asgi",
                   "ASGI_MOUNT_MAX",
                   ASGI_MOUNT_MAX,
                   "count",
                   "max path mounts per ASGI server");

PM_METAL_MEM_LIMIT(g_pm_metal_lim_net_asgi_ASGI_SRV_MAX,
                   "net.asgi",
                   "ASGI_SRV_MAX",
                   ASGI_SRV_MAX,
                   "count",
                   "max concurrent ASGI listen servers");

PM_METAL_MEM_LIMIT(g_pm_metal_lim_net_asgi_ASGI_HDR_MAX,
                   "net.asgi",
                   "ASGI_HDR_MAX",
                   ASGI_HDR_MAX,
                   "bytes",
                   "HTTP request header parse buffer");

PM_METAL_MEM_LIMIT(g_pm_metal_lim_net_asgi_ASGI_APP_SLOTS,
                   "net.asgi",
                   "ASGI_APP_SLOTS",
                   ASGI_APP_SLOTS,
                   "count",
                   "ASGI app_h registry slots");

PM_METAL_MEM_LIMIT(g_pm_metal_lim_net_tls_TLS_WIRE_MAX,
                   "net.tls",
                   "PM_METAL_TLS_WIRE_MAX",
                   PM_METAL_TLS_WIRE_MAX,
                   "bytes",
                   "mbedTLS plaintext/ciphertext wire buf");

PM_METAL_MEM_LIMIT(g_pm_metal_lim_py_BLOB_BYTES,
                   "py",
                   "PM_METAL_PY_BLOB_BYTES",
                   PM_METAL_PY_BLOB_BYTES,
                   "bytes",
                   "primary MicroPython heap blob");

PM_METAL_MEM_LIMIT(g_pm_metal_lim_py_ISOLATED_BLOB_BYTES,
                   "py",
                   "PM_METAL_PY_ISOLATED_BLOB_BYTES",
                   PM_METAL_PY_ISOLATED_BLOB_BYTES,
                   "bytes",
                   "isolated MicroPython session heap");
