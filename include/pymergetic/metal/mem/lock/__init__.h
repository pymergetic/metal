#ifndef PYMERGETIC_METAL_MEM_LOCK_H_
#define PYMERGETIC_METAL_MEM_LOCK_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Spin / Mutex are each one AtomicU32 word in RS. */
typedef struct pm_metal_mem_lock_spin {
    uint32_t state;
} pm_metal_mem_lock_spin_t;

typedef struct pm_metal_mem_lock_mutex {
    uint32_t state;
} pm_metal_mem_lock_mutex_t;

void pm_metal_mem_lock_mutex_init(pm_metal_mem_lock_mutex_t *m);
void pm_metal_mem_lock_mutex_lock(const pm_metal_mem_lock_mutex_t *m);
int32_t pm_metal_mem_lock_mutex_try_lock(const pm_metal_mem_lock_mutex_t *m);
void pm_metal_mem_lock_mutex_unlock(const pm_metal_mem_lock_mutex_t *m);
void pm_metal_mem_lock_spin_init(pm_metal_mem_lock_spin_t *s);
void pm_metal_mem_lock_spin_lock(const pm_metal_mem_lock_spin_t *s);
int32_t pm_metal_mem_lock_spin_try_lock(const pm_metal_mem_lock_spin_t *s);
void pm_metal_mem_lock_spin_unlock(const pm_metal_mem_lock_spin_t *s);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_MEM_LOCK_H_ */
