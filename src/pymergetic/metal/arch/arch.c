#include "pymergetic/metal/arch.h"

pm_metal_arch_id_t pm_metal_arch_current(void)
{
#if defined(PM_METAL_CFG_ARCH_WASM) && PM_METAL_CFG_ARCH_WASM
    return PM_METAL_ARCH_ID_WASM;
#elif defined(PM_METAL_CFG_ARCH_X86) && PM_METAL_CFG_ARCH_X86
    return PM_METAL_ARCH_ID_X86;
#elif defined(PM_METAL_CFG_ARCH_X86_64) && PM_METAL_CFG_ARCH_X86_64
    return PM_METAL_ARCH_ID_X86_64;
#elif defined(__wasm__) || defined(__EMSCRIPTEN__)
    return PM_METAL_ARCH_ID_WASM;
#elif defined(__i386__) || defined(_M_IX86)
    return PM_METAL_ARCH_ID_X86;
#else
    return PM_METAL_ARCH_ID_X86_64;
#endif
}

const char *pm_metal_arch_name(pm_metal_arch_id_t id)
{
    switch (id) {
    case PM_METAL_ARCH_ID_X86:
        return "x86";
    case PM_METAL_ARCH_ID_X86_64:
        return "x86_64";
    case PM_METAL_ARCH_ID_WASM:
        return "wasm";
    default:
        return "unknown";
    }
}

pm_metal_arch_firmware_t pm_metal_arch_firmware(void)
{
#if defined(PM_METAL_CFG_FW_UNIX) && PM_METAL_CFG_FW_UNIX
    return PM_METAL_FW_ID_UNIX;
#elif defined(PM_METAL_CFG_FW_BROWSER) && PM_METAL_CFG_FW_BROWSER
    return PM_METAL_FW_ID_BROWSER;
#elif defined(PM_METAL_CFG_FW_UEFI) && PM_METAL_CFG_FW_UEFI
    return PM_METAL_FW_ID_UEFI;
#elif defined(PM_METAL_CFG_FW_BIOS) && PM_METAL_CFG_FW_BIOS
    return PM_METAL_FW_ID_BIOS;
#elif defined(METAL_BOARD_UEFI) && METAL_BOARD_UEFI
    return PM_METAL_FW_ID_UEFI;
#elif defined(METAL_BOARD_UEFI) && !METAL_BOARD_UEFI
    return PM_METAL_FW_ID_BIOS;
#elif defined(__wasm__) || defined(__EMSCRIPTEN__)
    return PM_METAL_FW_ID_BROWSER;
#else
    return PM_METAL_FW_ID_NONE;
#endif
}
