/* Frozen / no-glue seats — self-register via section. */
#include <pymergetic/metal/reg/seats.h>
#include <stddef.h>
#include <stdint.h>

int32_t pm_metal_inspect_seat_test(void) __attribute__((weak));
int32_t pm_metal_net_microdot_seat_test(void) __attribute__((weak));
PM_METAL_REG_SEAT(g_pm_seat_arch, "pymergetic.metal.arch", PM_METAL_REG_SEAT_FROZEN, 1, 1, NULL);
PM_METAL_REG_SEAT(g_pm_seat_arch_wasm, "pymergetic.metal.arch.wasm", PM_METAL_REG_SEAT_FROZEN, 1, 1, NULL);
PM_METAL_REG_SEAT(g_pm_seat_arch_x86, "pymergetic.metal.arch.x86", PM_METAL_REG_SEAT_FROZEN, 1, 1, NULL);
PM_METAL_REG_SEAT(g_pm_seat_arch_x86_64, "pymergetic.metal.arch.x86_64", PM_METAL_REG_SEAT_FROZEN, 1, 1, NULL);
PM_METAL_REG_SEAT(g_pm_seat_inspect, "pymergetic.metal.inspect", PM_METAL_REG_SEAT_FROZEN, 1, 1, pm_metal_inspect_seat_test);
PM_METAL_REG_SEAT(g_pm_seat_net_microdot, "pymergetic.metal.net.microdot", PM_METAL_REG_SEAT_FROZEN, 1, 1, pm_metal_net_microdot_seat_test);
PM_METAL_REG_SEAT(g_pm_seat_unix_x86, "pymergetic.metal.unix.x86", PM_METAL_REG_SEAT_FROZEN, 1, 1, NULL);
PM_METAL_REG_SEAT(g_pm_seat_unix_x86_64, "pymergetic.metal.unix.x86_64", PM_METAL_REG_SEAT_FROZEN, 1, 1, NULL);
