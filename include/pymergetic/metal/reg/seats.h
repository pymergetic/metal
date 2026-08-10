/*
 * Seat registry — circular ring of µPy import / smoke faces.
 *
 * Seats are registered at runtime from the RegMod floor load path and
 * smoke helpers (`pm_metal_reg_seat_register` / `_ex`). The old linker
 * section (`PM_METAL_REG_SEAT`) is retired.
 *
 * Path law: seat.path == the dotted import name (never rewritten).
 *   pymergetic.metal.net.ssh  → import pymergetic.metal.net.ssh
 *   framebuf                  → import framebuf
 *   wasmmod.ticks / acme.x    → import that string as-is
 * No metal-relative short forms; no secret prefix; no strip-on-mod-load.
 */
#ifndef PYMERGETIC_METAL_REG_SEATS_H_
#define PYMERGETIC_METAL_REG_SEATS_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PM_METAL_REG_SEAT_PATH_MAX
#define PM_METAL_REG_SEAT_PATH_MAX 96
#endif

#ifndef PM_METAL_REG_JSON_CAP
#define PM_METAL_REG_JSON_CAP 24576
#endif

typedef enum pm_metal_reg_seat_kind {
    PM_METAL_REG_SEAT_GLUE = 0,
    PM_METAL_REG_SEAT_FROZEN = 1,
} pm_metal_reg_seat_kind_t;

#define PM_METAL_REG_SEAT_F_TEST_ONLY 1u

/** Seat / smoke test. Return 0 on success, nonzero on failure. */
typedef int32_t (*pm_metal_reg_seat_test_fn)(void);

typedef struct pm_metal_reg_seat {
    const char *path;
    pm_metal_reg_seat_kind_t kind;
    uint8_t fw;
    uint8_t browser;
    uint8_t flags;
    uint8_t linked;
    pm_metal_reg_seat_test_fn test;
    struct pm_metal_reg_seat *prev;
    struct pm_metal_reg_seat *next;
} pm_metal_reg_seat_t;

/* Retired linker macros — fail loudly if reintroduced. */
#define PM_METAL_REG_SEAT(...) \
    _Static_assert(0, "PM_METAL_REG_SEAT retired — use pm_metal_reg_seat_register")
#define PM_METAL_REG_SEAT_TEST_ONLY(...) \
    _Static_assert(0, "PM_METAL_REG_SEAT_TEST_ONLY retired — use pm_metal_reg_seat_register_ex")

/** Meyers ring head (any node), or NULL if empty. */
pm_metal_reg_seat_t *pm_metal_reg_seat_ring(void);

/** Splice node into ring if not already linked. Same path updates in place. */
int32_t pm_metal_reg_seat_splice(pm_metal_reg_seat_t *node);

/** Idempotent boot marker (linker seat section removed). */
void pm_metal_reg_seats_boot(void);

/**
 * Dynamic / user registration. Allocates a heap node (path copied).
 * Same path updates kind/flags/test on the existing node.
 */
int32_t pm_metal_reg_seat_register(const char *path, pm_metal_reg_seat_kind_t kind, uint8_t fw,
                                   uint8_t browser, pm_metal_reg_seat_test_fn test);
int32_t pm_metal_reg_seat_register_ex(const char *path, pm_metal_reg_seat_kind_t kind, uint8_t fw,
                                      uint8_t browser, uint8_t flags, pm_metal_reg_seat_test_fn test);

int32_t pm_metal_reg_seat_set_test(const char *path, pm_metal_reg_seat_test_fn fn);

/** Pack / RegMod load: splice full module name as-is (path == import). */
void pm_metal_reg_seat_on_mod_load(const char *full_module);

uint32_t pm_metal_reg_seat_count(void);

int32_t pm_metal_reg_seat_at(uint32_t index, char *path_buf, uint32_t path_cap, int32_t *kind,
                             int32_t *fw, int32_t *browser, int32_t *has_test);

int32_t pm_metal_reg_seats_json(char *buf, uint32_t buf_len);

/**
 * Heap seats JSON — linked growth then flatten. Caller pm_metal_mem_free(*out_buf).
 * Returns length or -1.
 */
int32_t pm_metal_reg_seats_json_heap(char **out_buf);

/**
 * Walk ring: custom test, or default import proof (skipped for TEST_ONLY).
 * Prints `reg seats ok N` / `upy ok` on success.
 */
int32_t pm_metal_reg_run_tests(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_REG_SEATS_H_ */
