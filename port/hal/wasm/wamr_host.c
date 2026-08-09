/*
 * Browser seat-local wamr_host HAL — nested guest stays fail-closed here
 * (not in a port dump). FW links real RS + WAMR port instead.
 */
#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/wamr_host/__init__.h>

int32_t pm_metal_wasm_fetch_register(const uint8_t *full_module, const char *url, const uint8_t *sig,
                                     uint32_t sig_len)
{
    (void)full_module;
    (void)url;
    (void)sig;
    (void)sig_len;
    return -1;
}

int32_t pm_metal_wasm_proof_fetch(void)
{
    return -1;
}

uint32_t pm_metal_wasm_guest_coro_create_for(const uint8_t *full_module, uint32_t state_bytes)
{
    (void)full_module;
    (void)state_bytes;
    return 0;
}

int32_t pm_metal_wasm_ready(void)
{
    return 0;
}

int32_t pm_metal_wasm_init(void)
{
    return -1;
}

void pm_metal_wasm_shutdown(void) {}

int32_t pm_metal_wasm_load(const uint8_t *full_module, const uint8_t *bytes, uint32_t len)
{
    (void)full_module;
    (void)bytes;
    (void)len;
    return -1;
}

int32_t pm_metal_wasm_image(const uint8_t *full_module, const uint8_t **out_bytes, uint32_t *out_len)
{
    (void)full_module;
    if (out_bytes) {
        *out_bytes = NULL;
    }
    if (out_len) {
        *out_len = 0;
    }
    return -1;
}

int32_t pm_metal_wasm_register(const uint8_t *full_module)
{
    (void)full_module;
    return -1;
}

int32_t pm_metal_wasm_load_register(const uint8_t *full_module, const uint8_t *bytes, uint32_t len)
{
    (void)full_module;
    (void)bytes;
    (void)len;
    return -1;
}

int32_t pm_metal_wasm_load_verified(const uint8_t *full_module, const uint8_t *bytes, uint32_t len,
                                    const uint8_t *sig, uint32_t sig_len)
{
    (void)full_module;
    (void)bytes;
    (void)len;
    (void)sig;
    (void)sig_len;
    return -1;
}

int32_t pm_metal_wasm_unload(const uint8_t *full_module)
{
    (void)full_module;
    return -1;
}

int32_t pm_metal_wasm_call0(const uint8_t *full_module, const uint8_t *func)
{
    (void)full_module;
    (void)func;
    return -1;
}

int32_t pm_metal_wasm_proof_stress(void)
{
    return -1;
}

int32_t pm_metal_wasm_proof(void)
{
    return -1;
}
