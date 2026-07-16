/*
 * Memory — zephyr shared arena_budget helpers (plat-private).
 * See docs/RUNTIME.md Bring-up plan §5.
 */
#ifndef PYMERGETIC_METAL_MEMORY_BUDGET_H_
#define PYMERGETIC_METAL_MEMORY_BUDGET_H_

#include <stdint.h>

/* impl: zephyr — src/zephyr/pymergetic/metal/memory/budget.c */
uint64_t pm_metal_memory_zephyr_link_used(void);
uint64_t pm_metal_memory_zephyr_arena_budget(void);

/* Take up to `requested` from the shared remaining budget (first-come).
 * impl: zephyr — src/zephyr/pymergetic/metal/memory/budget.c */
uint64_t pm_metal_memory_zephyr_budget_take(uint64_t requested);
void pm_metal_memory_zephyr_budget_reset(void);

/* impl: zephyr — src/zephyr/pymergetic/metal/memory/budget.c */
void *pm_metal_memory_zephyr_pool_alloc(uint64_t bytes);
void pm_metal_memory_zephyr_pool_free(void *ptr);

#endif /* PYMERGETIC_METAL_MEMORY_BUDGET_H_ */
