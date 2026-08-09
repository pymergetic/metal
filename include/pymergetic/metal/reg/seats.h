/*
 * Seat registry — self-registering circular ring (no central path blob).
 *
 * Each owning TU emits PM_METAL_REG_SEAT(...). Boot walks the linker
 * section once and splices nodes into a Meyers ring. Dynamic / user mods
 * call pm_metal_reg_seat_register / on_mod_load to splice the same ring.
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

/** Fixed-size section row (LTO-safe walk). */
typedef struct pm_metal_reg_seat_table {
    pm_metal_reg_seat_t *seat;
    uint8_t _pad[16u - sizeof(void *)];
} pm_metal_reg_seat_table_t;

/**
 * Place one seat in `.pm_metal_seats.<path>` (SORT_BY_NAME).
 * `path_lit` is a string literal; `test_fn` may be a weak symbol (NULL if absent).
 */
#define PM_METAL_REG_SEAT(sym, path_lit, kind_, fw_, browser_, test_fn)        \
    static pm_metal_reg_seat_t sym = {                                         \
        .path = (path_lit),                                                    \
        .kind = (kind_),                                                       \
        .fw = (uint8_t)(fw_),                                                  \
        .browser = (uint8_t)(browser_),                                        \
        .flags = 0u,                                                           \
        .linked = 0u,                                                          \
        .test = (test_fn),                                                     \
        .prev = 0,                                                             \
        .next = 0,                                                             \
    };                                                                         \
    static const pm_metal_reg_seat_table_t sym##_tbl __attribute__((           \
        used, section(".pm_metal_seats." path_lit), aligned(16))) = {          \
        .seat = &sym,                                                          \
        ._pad = { 0 },                                                         \
    }

#define PM_METAL_REG_SEAT_TEST_ONLY(sym, path_lit, test_fn)                    \
    static pm_metal_reg_seat_t sym = {                                         \
        .path = (path_lit),                                                    \
        .kind = PM_METAL_REG_SEAT_GLUE,                                        \
        .fw = 1u,                                                              \
        .browser = 1u,                                                         \
        .flags = PM_METAL_REG_SEAT_F_TEST_ONLY,                                \
        .linked = 0u,                                                          \
        .test = (test_fn),                                                     \
        .prev = 0,                                                             \
        .next = 0,                                                             \
    };                                                                         \
    static const pm_metal_reg_seat_table_t sym##_tbl __attribute__((           \
        used, section(".pm_metal_seats." path_lit), aligned(16))) = {          \
        .seat = &sym,                                                          \
        ._pad = { 0 },                                                         \
    }

extern const pm_metal_reg_seat_table_t __pm_metal_seats_start[] __attribute__((weak));
extern const pm_metal_reg_seat_table_t __pm_metal_seats_end[] __attribute__((weak));

/** Meyers ring head (any node), or NULL if empty. */
pm_metal_reg_seat_t *pm_metal_reg_seat_ring(void);

/** Splice node into ring if not already linked. Same path updates in place. */
int32_t pm_metal_reg_seat_splice(pm_metal_reg_seat_t *node);

/** Section walk → splice; idempotent. */
void pm_metal_reg_seats_boot(void);

/**
 * Dynamic / user registration. Allocates a heap node (path copied).
 * Same path updates kind/flags/test on the existing node.
 */
int32_t pm_metal_reg_seat_register(const char *path, pm_metal_reg_seat_kind_t kind, uint8_t fw,
                                   uint8_t browser, pm_metal_reg_seat_test_fn test);

int32_t pm_metal_reg_seat_set_test(const char *path, pm_metal_reg_seat_test_fn fn);

/** Pack / RegMod load: splice full module name as-is (path == import). */
void pm_metal_reg_seat_on_mod_load(const char *full_module);

uint32_t pm_metal_reg_seat_count(void);

int32_t pm_metal_reg_seat_at(uint32_t index, char *path_buf, uint32_t path_cap, int32_t *kind,
                             int32_t *fw, int32_t *browser, int32_t *has_test);

int32_t pm_metal_reg_seats_json(char *buf, uint32_t buf_len);

/**
 * Walk ring: custom test, or default import proof (skipped for TEST_ONLY).
 * Prints `reg seats ok N` / `upy ok` on success.
 */
int32_t pm_metal_reg_run_tests(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_REG_SEATS_H_ */
