#include <stddef.h>
#include <stdint.h>
#include "pymergetic/metal/mem.h"
void *pm_metal_bge_port_alloc_pages(uintptr_t bytes) {
  if (bytes == 0) return NULL;
  void *p = pm_metal_mem_memalign(4096u, (size_t)bytes);
  if (p == NULL) p = pm_metal_mem_map((size_t)bytes);
  return p;
}
