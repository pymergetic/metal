#ifndef PM_METAL_CONSOLE_H_
#define PM_METAL_CONSOLE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pm_metal_console_sink_fn)(const uint8_t *data, size_t n, void *user);

/* Seed ring from a board buffer. Returns 0 on success. */
int32_t pm_metal_console_init(uint8_t *buf, size_t cap);

int32_t pm_metal_console_ready(void);

/* Sync enqueue (never parks). Drops oldest when full. Also fans to live sink. */
size_t pm_metal_console_write(const uint8_t *data, size_t n);

/* Replay history then set live sink (one viewport for now). */
int32_t pm_metal_console_attach(pm_metal_console_sink_fn sink, void *user);
/* Live sink only — no history replay. */
int32_t pm_metal_console_set_sink(pm_metal_console_sink_fn sink, void *user);
void pm_metal_console_detach(void);

uint64_t pm_metal_console_seq(void);
size_t pm_metal_console_len(void);

#ifdef __cplusplus
}
#endif

#endif
