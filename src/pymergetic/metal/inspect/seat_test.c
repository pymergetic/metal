/*
 * Co-located reg seat test for inspect (C dispatch + ledger routes).
 */
#include <pymergetic/metal/inspect/__init__.h>
#include <pymergetic/metal/reg/ledger.h>
#include <pymergetic/metal/reg/seats.h>

#include <string.h>

extern void uart_puts(const char *s);

static int body_has(const char *body, const char *needle)
{
    return body != NULL && needle != NULL && strstr(body, needle) != NULL;
}

int32_t pm_metal_inspect_seat_test(void)
{
    int status = 0;
    char body[8192];
    int32_t r;

    if (pm_metal_inspect_init() != 0) {
        uart_puts("inspect fail init\n");
        return -1;
    }

    r = pm_metal_inspect_handle("GET", "/health", &status, body, sizeof(body));
    if (r <= 0 || status != 200 || !body_has(body, "ok")) {
        uart_puts("inspect fail health\n");
        return -1;
    }
    r = pm_metal_inspect_handle("GET", "/capabilities", &status, body, sizeof(body));
    if (r <= 0 || status != 200 || !body_has(body, "metal")) {
        uart_puts("inspect fail caps\n");
        return -1;
    }
    r = pm_metal_inspect_handle("GET", "/inspect/self", &status, body, sizeof(body));
    if (r <= 0 || status != 200 || !body_has(body, "kernel") || !body_has(body, "has_source")) {
        uart_puts("inspect fail self\n");
        return -1;
    }
    r = pm_metal_inspect_handle("GET", "/inspect/", &status, body, sizeof(body));
    if (r > 0) {
        uart_puts("inspect fail slash\n");
        return -1;
    }

    if (pm_metal_reg_ledger_seed_pilot() != 0) {
        uart_puts("inspect fail seed\n");
        return -1;
    }
    if (pm_metal_reg_ledger_method_count() <= 0) {
        uart_puts("inspect fail method_count\n");
        return -1;
    }

    r = pm_metal_inspect_handle("GET", "/inspect/reg", &status, body, sizeof(body));
    if (r <= 0 || status != 200 || !body_has(body, "method_count") || !body_has(body, "gap_count")
        || !body_has(body, "completeness_url")) {
        uart_puts("inspect fail reg\n");
        return -1;
    }
    r = pm_metal_inspect_handle("GET", "/inspect/reg/completeness", &status, body, sizeof(body));
    if (r <= 0 || status != 200
        || !(body_has(body, "method_count") || body_has(body, "reg completeness"))) {
        uart_puts("inspect fail completeness\n");
        return -1;
    }
    r = pm_metal_inspect_handle("GET", "/inspect/reg/completeness?fmt=tree&gaps_only=1", &status, body,
                                sizeof(body));
    if (r <= 0 || status != 200 || !body_has(body, "reg completeness")) {
        uart_puts("inspect fail completeness tree\n");
        return -1;
    }
    r = pm_metal_inspect_handle("GET", "/inspect/reg/pymergetic.metal.async", &status, body,
                                sizeof(body));
    if (r <= 0 || status != 200 || !body_has(body, "yield")) {
        uart_puts("inspect fail reg module\n");
        return -1;
    }
    r = pm_metal_inspect_handle("GET", "/inspect/reg/pymergetic.metal.async/yield", &status, body,
                                sizeof(body));
    if (r <= 0 || status != 200 || !body_has(body, "callees") || !body_has(body, "c_runner")) {
        uart_puts("inspect fail reg method\n");
        return -1;
    }

    /* Seats table visible via the same Inspect surface. */
    if (pm_metal_reg_seat_count() == 0) {
        uart_puts("inspect fail seat_count\n");
        return -1;
    }
    if (pm_metal_reg_seats_json(body, (uint32_t)sizeof(body)) < 0
        || !body_has(body, "pymergetic.metal.net.ssh")) {
        uart_puts("inspect fail seats_json\n");
        return -1;
    }

    uart_puts("inspect ok\n");
    return 0;
}
