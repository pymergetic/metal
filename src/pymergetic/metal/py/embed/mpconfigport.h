/* Metal MicroPython port config — freestanding; Metal owns I/O / tasks / FS. */
#include <stdint.h>
#include <stddef.h>

typedef intptr_t  mp_int_t;
typedef uintptr_t mp_uint_t;
typedef long      mp_off_t;

#ifndef alloca
#define alloca(n) __builtin_alloca(n)
#endif

#define MICROPY_MPHALPORT_H "port/mphalport.h"

/* ---- baseline ---------------------------------------------------------- */
/* MINIMUM = smallest ROM feature set; we opt back in below. Not "we rebuild
 * µPy from scratch" — same upstream sources, just a lean CONFIG. */
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_MINIMUM)

#define MICROPY_HW_BOARD_NAME  "metal"
#define MICROPY_HW_MCU_NAME    "metal"
#define MP_STATE_PORT          MP_STATE_VM
#define MICROPY_ALLOC_PATH_MAX (256)

/* ---- spike needs these on ---------------------------------------------- */
#define MICROPY_ENABLE_COMPILER        (1) /* shell python <script> / run_str */
#define MICROPY_ENABLE_GC              (1) /* always-on MAP blob heap */
#define MICROPY_PY_GC                  (1) /* gc module for diagnostics */
#define MICROPY_ENABLE_EXTERNAL_IMPORT (1) /* import from FS / zip path */
#define MICROPY_PY_ASYNC_AWAIT         (1) /* await → Metal handles */
#define MICROPY_PY_SYS                 (1) /* sys module (MINIMUM leaves this off) */
#define MICROPY_PY_SYS_PATH            (1) /* search path for imports */
#define MICROPY_HELPER_REPL \
  (1) /* mp_repl_continue_with_input — REPL's multi-line block detection */

/* ---- small, self-contained C extmods the "Easy" stdlib.zip pack needs -- */
/* Each backs a pure-Python micropython-lib shim under mods/py/stdlib_src/
 * that re-exports/wraps it via the "import ufoo forces builtin foo" alias
 * (py/objmodule.c:193-206) — see docs/MICROPYTHON.md's stdlib categorization.
 * struct's format codes are int-only in practice: MICROPY_PY_BUILTINS_FLOAT
 * stays off below, so 'f'/'d' format chars would fail at runtime, not
 * compile time (py/binary.c has no unconditional float reference). None of
 * these reopen the floats/math/longint decision. */
#define MICROPY_PY_COLLECTIONS              (1) /* ucollections — collections/, contextlib's deque */
#define MICROPY_PY_COLLECTIONS_DEQUE        (1) /* collections.deque — contextlib/ needs it */
#define MICROPY_PY_COLLECTIONS_DEQUE_ITER   (1)
#define MICROPY_PY_COLLECTIONS_DEQUE_SUBSCR (1)
#define MICROPY_PY_BINASCII                 (1) /* ubinascii — binascii/, base64/'s a2b_/b2a_ calls */
#define MICROPY_PY_BUILTINS_BYTES_HEX       (1) /* bytes.hex()/fromhex() — binascii hexlify/unhexlify */
#define MICROPY_PY_HEAPQ                    (1) /* uheapq — heapq/ */
#define MICROPY_PY_ERRNO                    (1) /* uerrno — errno/ */
#define MICROPY_PY_STRUCT                   (1) /* ustruct — struct/ (int formats only, no floats) */

/* ---- CORE_FEATURES-gated grammar/builtins the Easy pack actually needs -
 * MINIMUM leaves these off by default (py/mpconfig.h gates them at
 * AT_LEAST_CORE_FEATURES) but slicing (x[a:b]) in particular is used
 * pervasively by ordinary Python (heapq/bisect/argparse/... all do it) —
 * without this the *grammar* itself has no production for "[a:b]" and
 * every such module fails to even parse (SyntaxError), not just import. */
#define MICROPY_PY_BUILTINS_SLICE     (1) /* x[a:b] / x[a:b:c] subscript syntax */
#define MICROPY_PY_BUILTINS_SET       (1) /* {1, 2, 3} literal + set ops */
#define MICROPY_PY_BUILTINS_ENUMERATE (1) /* enumerate() — argparse/ uses it */
#define MICROPY_PY_SYS_EXC_INFO \
  (1) /* sys.exc_info() — traceback/'s print_exc()/format_exc(); the \
       * backing MP_STATE_VM(cur_exception) root pointer (py/vm.c) is set \
       * unconditionally on every caught exception regardless of this \
       * flag, so enabling it only compiles in the accessor, no new \
       * runtime cost/behavior elsewhere. */
#define MICROPY_PY_BUILTINS_STR_OP_MODULO \
  (1) /* "%s" % x — logging/'s message formatting; also a latent gap in \
       * argparse/'s own error-message paths (unexercised by \
       * PY_PROOF_STDLIB's happy-path parse, which never hit them) */

/* ---- Metal provides these — keep µPy copies off ------------------------ */
#define MICROPY_PY_THREAD         (0) /* Python task = Metal task; no GIL */
#define MICROPY_ENABLE_SCHEDULER  (0) /* Metal async, not mp_sched */
#define MICROPY_VFS               (0) /* Metal FS + py_port_stubs import_stat/open */
#define MICROPY_MODULE_FROZEN_MPY (0) /* stdlib = signed zip, not frozen .mpy */
#define MICROPY_MODULE_FROZEN_STR (0) /* same — zip / HTTP seed later */

/* ---- bring-up off (link grease; re-enable when stubs/lib ready) -------- */
/* No libm / half-wired builtin modules until we wire them cleanly. */
#define MICROPY_FLOAT_IMPL          (MICROPY_FLOAT_IMPL_NONE)
#define MICROPY_PY_BUILTINS_FLOAT   (0)
#define MICROPY_PY_BUILTINS_COMPLEX (0)
#define MICROPY_PY_MATH             (0)
#define MICROPY_LONGINT_IMPL        (MICROPY_LONGINT_IMPL_NONE)
#define MICROPY_PY_IO               (0) /* open stub only; full io later via Metal */
#define MICROPY_PY_SYS_ARGV         (0)
#define MICROPY_ERROR_REPORTING     (MICROPY_ERROR_REPORTING_NORMAL)
