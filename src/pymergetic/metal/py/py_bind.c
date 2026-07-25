/** @file Bind table placeholder (rows installed via metal.aio C module). */
#include <pymergetic/metal/py/py.h>

int pm_metal_py_bind_table(const pm_metal_py_bind_t *rows, size_t n)
{
  (void)rows;
  (void)n;
  /* Spike: metal.aio is registered in py_aio_mod_init. */
  return 0;
}
