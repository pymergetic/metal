/*
 * Metal memory — guest/host dual ABI (same TLSF heap).
 *
 * Host: real pointers from pm_metal_mem_alloc / free.
 * Guest: same names; returned “pointers” are opaque host cookies — do not
 * dereference in wasm. Prefer coro_alloc for stackless T* step frames.
 *
 * Wasm linear memory = statics + short in-step stack only.
 *
 * impl: src/pymergetic/metal/runtime/mem/mem.c
 *       src/pymergetic/metal/runtime/mem/mem_natives.c (WASI)
 */
#ifndef PYMERGETIC_METAL_RUNTIME_MEM_MEM_H_
#define PYMERGETIC_METAL_RUNTIME_MEM_MEM_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_MEM_LOCAL     0u
#define PM_METAL_MEM_HEAP      PM_METAL_MEM_LOCAL
#define PM_METAL_MEM_SHARED    (1u << 0)
#define PM_METAL_MEM_MAP       (1u << 2)
#define PM_METAL_MEM_CPU_MASK  0x0000ff00u
#define PM_METAL_MEM_CPU_SHIFT 8
#define PM_METAL_MEM_CPU(k) \
  ((((uint32_t)(k) << PM_METAL_MEM_CPU_SHIFT) & PM_METAL_MEM_CPU_MASK) | (1u << 1))

#define PM_METAL_MEM_ID_NONE 0u

/** 4 KiB MMU/arena page size — not an EFI_PAGE_SIZE alias, just what it is. */
#define PM_METAL_MEM_PAGE_SIZE 4096u

/**
 * Metal heap pointer (not a random void*).
 * Host: real TLSF pointer.
 * Guest mem_alloc/free: opaque host cookie — do not dereference.
 * Guest coro frame (via async): linear T* alias for the current step.
 */
typedef void *pm_metal_ptr_t;

typedef uint32_t pm_metal_mem_flags_t;
typedef uint32_t pm_metal_mem_id_t;

typedef enum {
  PM_METAL_MEM_SHARED_CLASS_64 = 0,
  PM_METAL_MEM_SHARED_CLASS_256,
  PM_METAL_MEM_SHARED_CLASS_1K,
  PM_METAL_MEM_SHARED_CLASS_4K,
  PM_METAL_MEM_SHARED_CLASS_COUNT
} pm_metal_mem_shared_class_t;

#define PM_METAL_MEM_WASI_MODULE "pymergetic.metal.mem"

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"

#define PM_METAL_MEM_IMPORT(name) PM_METAL_WASI_IMPORT(PM_METAL_MEM_WASI_MODULE, name)

/** Opaque host cookie (not a linear address). */
extern pm_metal_ptr_t pm_metal_mem_alloc(size_t               size,
                                         pm_metal_mem_flags_t where,
                                         pm_metal_mem_id_t    id)
  PM_METAL_MEM_IMPORT(pm_metal_mem_alloc);

extern void pm_metal_mem_free(pm_metal_ptr_t ptr) PM_METAL_MEM_IMPORT(pm_metal_mem_free);

/** Host cookie → guest linear (`dest` is a wasm linear offset). 0 = ok. */
extern int32_t pm_metal_mem_copy_out(pm_metal_ptr_t src, uint32_t dest, uint32_t n)
  PM_METAL_MEM_IMPORT(pm_metal_mem_copy_out);

/**
 * Host cookie[src_off .. src_off+n) → guest linear.
 * 0 = ok. Needed when durable data stays on TLSF (not mmap'd into linear).
 */
extern int32_t pm_metal_mem_copy_out_at(pm_metal_ptr_t src,
                                        uint32_t       src_off,
                                        uint32_t       dest,
                                        uint32_t n) PM_METAL_MEM_IMPORT(pm_metal_mem_copy_out_at);

/** Guest linear → host cookie. 0 = ok. */
extern int32_t pm_metal_mem_copy_in(pm_metal_ptr_t dest, uint32_t src, uint32_t n)
  PM_METAL_MEM_IMPORT(pm_metal_mem_copy_in);

#else /* host */

int      pm_metal_mem_init(void *arena, size_t bytes, unsigned n_cpus);
void     pm_metal_mem_set_cpu(unsigned cpu_id);
unsigned pm_metal_mem_cpu(void);

pm_metal_ptr_t pm_metal_mem_alloc(size_t size, pm_metal_mem_flags_t where, pm_metal_mem_id_t id);
pm_metal_ptr_t pm_metal_mem_lookup(pm_metal_mem_id_t id);
pm_metal_ptr_t pm_metal_mem_realloc(pm_metal_ptr_t ptr, size_t size);
void           pm_metal_mem_free(pm_metal_ptr_t ptr);

pm_metal_ptr_t pm_metal_mem_shared_alloc_class(pm_metal_mem_shared_class_t cls);
void           pm_metal_mem_shared_free(pm_metal_ptr_t ptr);

pm_metal_ptr_t pm_metal_mem_map(size_t bytes);
int            pm_metal_mem_unmap(pm_metal_ptr_t ptr, size_t bytes);

size_t   pm_metal_mem_arena_bytes(void);
size_t   pm_metal_mem_map_bytes(void);
size_t   pm_metal_mem_heap_bytes(void);
size_t   pm_metal_mem_hole_bytes(void);
void     pm_metal_mem_heap_pool_bytes(size_t *used_out, size_t *free_out);
size_t   pm_metal_mem_local_bytes(void);
size_t   pm_metal_mem_shared_bytes(void);
size_t   pm_metal_mem_os_bytes(void);
unsigned pm_metal_mem_n_cpus(void);

void   pm_metal_mem_set_phys_bytes(size_t phys_bytes);
size_t pm_metal_mem_phys_bytes(void);

/** Host cookie → guest linear (`dest` is a wasm linear offset). 0 = ok. */
int32_t pm_metal_mem_copy_out(pm_metal_ptr_t src, uint32_t dest, uint32_t n);
/** Host cookie[src_off ..) → guest linear. 0 = ok. */
int32_t pm_metal_mem_copy_out_at(pm_metal_ptr_t src, uint32_t src_off, uint32_t dest, uint32_t n);
/** Guest linear → host cookie. 0 = ok. */
int32_t pm_metal_mem_copy_in(pm_metal_ptr_t dest, uint32_t src, uint32_t n);

#endif /* !__wasm__ */

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_RUNTIME_MEM_MEM_H_ */
