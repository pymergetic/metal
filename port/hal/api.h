/*
 * HAL face shared by all seats. Real (bios/efi) vs fake (wasm) backends
 * link the same symbols; boot.c only talks this API + metal core.
 */
#ifndef PM_METAL_PORT_HAL_API_H_
#define PM_METAL_PORT_HAL_API_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void pm_metal_hal_console_init(void);
void pm_metal_hal_console_puts(const char *s);
void pm_metal_hal_console_write(const char *s, size_t n);

/* Seat label for boot banner / tree (e.g. "x86_64", "wasm32"). */
const char *pm_metal_hal_cpu_label(void);

/* 1 = sim backends (browser); 0 = real firmware drivers. */
int pm_metal_hal_is_sim(void);

/*
 * Claim the Metal dual-span arena window.
 * Firmware: conventional/loader pages. Browser: aligned malloc (sim HAL).
 * Returns 0 on success; *base page-aligned, *bytes ≥ arena minimum.
 */
int pm_metal_hal_mem_claim(uint8_t **base, size_t *bytes);

/* Browser: seat budget from loadMicroPython({ heapsize }) before boot. */
void pm_metal_hal_mem_set_budget(size_t bytes);
size_t pm_metal_hal_mem_budget(void);

#ifdef __cplusplus
}
#endif

#endif
