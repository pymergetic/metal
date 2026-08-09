/* metal CDN `mp` — wasmmod browser IO + JS hooks. */
#define MICROPY_VARIANT_ENABLE_JS_HOOK (1)

#include "../../../../wasmmod/ports/micropython/webassembly/mpconfig_webassembly.h"

#if !defined(MICROPY_WASM_IO_OPS)
#error "mpconfig_webassembly.h must define MICROPY_WASM_IO_OPS"
#endif
