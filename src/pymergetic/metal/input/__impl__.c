/* pymergetic.metal.input — key ring; F-keys select; printables feed the focus. */
#include "pymergetic/metal/input/__exports__.h"

#include "pymergetic/metal/console.h"
#include "pymergetic/metal/drivers/input.h"

#include <string.h>

#ifndef PM_METAL_INPUT_Q
#define PM_METAL_INPUT_Q 32u
#endif

static pm_util_mem_arena_t *s_arena;
static int32_t s_q[PM_METAL_INPUT_Q];
static uint32_t s_head;
static uint32_t s_n;

int32_t pm_metal_input_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    memset(s_q, 0, sizeof(s_q));
    s_head = 0;
    s_n = 0;
    return 0;
}

void pm_metal_input_deinit(void) {
    s_head = 0;
    s_n = 0;
    s_arena = NULL;
}

int32_t pm_metal_input_push(int32_t key) {
    uint32_t i;
    if (s_arena == NULL || key < 0 || s_n >= PM_METAL_INPUT_Q) {
        return -1;
    }
    i = (s_head + s_n) % PM_METAL_INPUT_Q;
    s_q[i] = key;
    s_n++;
    return 0;
}

int32_t pm_metal_input_pop(void) {
    int32_t key;
    if (s_n == 0) {
        return -1;
    }
    key = s_q[s_head % PM_METAL_INPUT_Q];
    s_head = (s_head + 1u) % PM_METAL_INPUT_Q;
    s_n--;
    return key;
}

int32_t pm_metal_input_count(void) {
    return (int32_t)s_n;
}

static int32_t feedable(int32_t key) {
    return key == '\n' || key == '\t' || key == '\b' || (key >= 32 && key < 127);
}

int32_t pm_metal_input_feed_console(void) {
    int32_t n = 0;
    int32_t key;
    if (s_arena == NULL) {
        return -1;
    }
    while ((key = pm_metal_input_pop()) >= 0) {
        if (key >= PM_METAL_INPUT_KEY_F1 && key <= PM_METAL_INPUT_KEY_F6) {
            if (pm_metal_console_select(key - PM_METAL_INPUT_KEY_F1) == 0) {
                n++;
            }
            continue;
        }
        if (feedable(key)) {
            char c = (char)key;
            (void)pm_metal_console_write_id((int32_t)pm_metal_console_id(), &c, 1);
            n++;
        }
    }
    return n;
}

int32_t pm_metal_input_up(void) {
    int32_t h;
    if (s_arena == NULL) {
        return -1;
    }
    (void)pm_metal_console_select(0);
    (void)pm_metal_drivers_input_poll_all();
    h = pm_metal_drivers_input_by_compat("virtio-input", 0);
    if (h < 0) {
        h = pm_metal_drivers_input_by_compat("ps2", 0);
    }
    if (h < 0) {
        return -1;
    }
    if (pm_metal_drivers_input_inject(h, (int32_t)'A') != 0) {
        return -1;
    }
    if (pm_metal_input_feed_console() < 1) {
        return -1;
    }
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.input, pm_metal_input_init, pm_metal_input_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.input, pm_metal_input_deinit, pm_metal_input_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.input, pm_metal_input_push, pm_metal_input_push, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.input, pm_metal_input_pop, pm_metal_input_pop, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.input, pm_metal_input_count, pm_metal_input_count, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.input, pm_metal_input_feed_console, pm_metal_input_feed_console, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.input, pm_metal_input_up, pm_metal_input_up, int32_t(void));

PM_MOD_BOOT_C(pymergetic.metal.input, pm_metal_input_init, pm_metal_input_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.input, pymergetic.metal.console);
