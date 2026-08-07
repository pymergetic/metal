#include <stdint.h>

// Include board name overrides before feature toggles that use them.
#include "mpconfigboard.h"

#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_MINIMUM)

#define MICROPY_ENABLE_COMPILER           (1)
#define MICROPY_ENABLE_GC                 (1)
#define MICROPY_HELPER_REPL               (1)
#define MICROPY_HELPER_LEXER_UNIX         (0)
#define MICROPY_ENABLE_EXTERNAL_IMPORT    (0)
#define MICROPY_MODULE_FROZEN_MPY         (0)
#define MICROPY_PY_ASYNCIO                (0)

#define MICROPY_ALLOC_PATH_MAX            (256)
#define MICROPY_ALLOC_PARSE_CHUNK_INIT    (16)

#define MICROPY_ERROR_REPORTING           (MICROPY_ERROR_REPORTING_TERSE)
#define MICROPY_USE_INTERNAL_ERRNO        (1)
#define MICROPY_USE_INTERNAL_PRINTF       (1)

#define MICROPY_FLOAT_IMPL                (MICROPY_FLOAT_IMPL_NONE)
#define MICROPY_LONGINT_IMPL              (MICROPY_LONGINT_IMPL_NONE)

#define MICROPY_PY_BUILTINS_COMPLEX       (0)
#define MICROPY_PY_BUILTINS_FLOAT         (0)
#define MICROPY_PY_BUILTINS_FROZENSET     (0)
#define MICROPY_PY_BUILTINS_SET           (0)
#define MICROPY_PY_BUILTINS_SLICE         (1)
#define MICROPY_PY_BUILTINS_PROPERTY      (0)
#define MICROPY_PY_BUILTINS_BYTEARRAY     (1)
#define MICROPY_PY_BUILTINS_MEMORYVIEW    (0)
#define MICROPY_PY_BUILTINS_ENUMERATE     (1)
#define MICROPY_PY_BUILTINS_FILTER        (0)
#define MICROPY_PY_BUILTINS_REVERSED      (1)
#define MICROPY_PY_BUILTINS_MIN_MAX       (1)
#define MICROPY_PY_BUILTINS_INPUT         (0)
#define MICROPY_PY_BUILTINS_HELP          (0)

#define MICROPY_PY_SYS_MODULES            (0)
#define MICROPY_PY_SYS_EXIT               (1)
#define MICROPY_PY_SYS_PATH               (0)
#define MICROPY_PY_SYS_ARGV               (0)
#define MICROPY_PY_SYS_PLATFORM           "metal"
#define MICROPY_PY_SYS_STDFILES           (0)

#define MICROPY_PY_GC                     (1)
#define MICROPY_PY_GC_COLLECT_RETVAL      (0)
#define MICROPY_PY___FILE__               (0)
#define MICROPY_PY_ARRAY                  (1)
#define MICROPY_PY_COLLECTIONS            (0)
#define MICROPY_PY_MATH                   (0)
#define MICROPY_PY_CMATH                  (0)
#define MICROPY_PY_IO                     (0)
#define MICROPY_PY_STRUCT                 (0)
#define MICROPY_PY_FRAMEBUF               (1)

#ifndef MICROPY_HEAP_SIZE
#define MICROPY_HEAP_SIZE                 (128 * 1024)
#endif

typedef long mp_off_t;

#ifndef alloca
#define alloca(n) __builtin_alloca(n)
#endif

#define MP_STATE_PORT MP_STATE_VM
