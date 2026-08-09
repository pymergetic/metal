#ifndef PM_METAL_CONSOLE_H_
#define PM_METAL_CONSOLE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PM_METAL_CONSOLE_MAX
#define PM_METAL_CONSOLE_MAX 6 /* #0 early + F2–F6 peers */
#endif

#ifndef PM_METAL_CONSOLE_VP_MAX
#define PM_METAL_CONSOLE_VP_MAX 8
#endif

typedef void (*pm_metal_console_sink_fn)(const uint8_t *data, size_t n, void *user);

typedef int32_t pm_metal_console_vp_id;

/* Seed console #0 ring from a board buffer. Returns 0 on success. */
int32_t pm_metal_console_init(uint8_t *buf, size_t cap);

/* Create/init console id (1..MAX-1) with its own ring buffer. */
int32_t pm_metal_console_create(int32_t id, uint8_t *buf, size_t cap);

int32_t pm_metal_console_ready(void);
int32_t pm_metal_console_ready_id(int32_t id);

/* Sync enqueue to console #0 (never parks). Fans to all viewports on #0. */
size_t pm_metal_console_write(const uint8_t *data, size_t n);
size_t pm_metal_console_write_id(int32_t id, const uint8_t *data, size_t n);

/*
 * Attach viewport to console (replay history). Returns viewport id ≥ 0.
 * Legacy: pm_metal_console_attach(sink,user) → attach to console #0.
 */
pm_metal_console_vp_id pm_metal_console_viewport_attach(int32_t console_id,
                                                        pm_metal_console_sink_fn sink, void *user);
int32_t pm_metal_console_attach(pm_metal_console_sink_fn sink, void *user);

/* Rebind existing viewport to another console (replay that console). */
int32_t pm_metal_console_viewport_rebind(pm_metal_console_vp_id vp, int32_t console_id);

void pm_metal_console_viewport_detach(pm_metal_console_vp_id vp);
void pm_metal_console_detach(void); /* detach all on console #0 (legacy) */

/* Live sink replace on console #0 without replay (legacy fan-out helper). */
int32_t pm_metal_console_set_sink(pm_metal_console_sink_fn sink, void *user);

uint64_t pm_metal_console_seq(void);
size_t pm_metal_console_len(void);
size_t pm_metal_console_copy_tail(uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
