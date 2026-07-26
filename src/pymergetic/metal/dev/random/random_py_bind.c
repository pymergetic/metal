/** @file Builtin pymergetic.metal.random.seed_u32 — one real-entropy draw,
 * used once at boot to seed the `random` extmod (extmod/modrandom.c) away
 * from its fixed compile-time default seed. Everything else `import
 * random` needs (getrandbits/seed/randrange/randint/choice) is that C
 * extmod directly — no Python glue module for it at all, see
 * docs/MICROPYTHON.md's stdlib categorization.
 */
#include <pymergetic/metal/dev/random/random.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>

#include "py/obj.h"

static mp_obj_t metal_random_seed_u32(void)
{
  uint32_t v = 0;

  (void)pm_metal_random(&v, sizeof(v));
  return pm_metal_py_int_new((int64_t)v);
}
static MP_DEFINE_CONST_FUN_OBJ_0(metal_random_seed_u32_obj, metal_random_seed_u32);
PM_METAL_PY_BIND(g_py_bind_random_seed_u32,
                 "pymergetic.metal.random",
                 "seed_u32",
                 metal_random_seed_u32_obj,
                 PM_METAL_PY_SYNC);
