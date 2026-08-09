/*
 * Compile-time externals rows for third-party stacks actually linked/frozen.
 * Collected into `.pm_metal_externals.*` on ELF (BIOS); UEFI/browser fall back
 * to dyn seed via pm_metal_externals_seed_fallback() when the section is empty.
 *
 * Product CORE (µPy seats including CDN wasm): microdot, crypto, lz4, …
 * Firmware-only: BCM57xx / lwIP / WireGuard (no virtio stack in browser).
 */
#include <pymergetic/metal/boot/externals.h>

/* ---- always (µPy product seats) ----------------------------------------- */

PM_METAL_EXTERNAL(g_pm_metal_ext_micropython, micropython, "1.29.0",
                  "https://github.com/micropython/micropython",
                  "REPL + frozen asyncio/extmods (Metal async is separate)");

PM_METAL_EXTERNAL(g_pm_metal_ext_tlsf, tlsf, "3.1", "http://tlsf.baisoku.org",
                  "Two-Level Segregated Fit host heap");

PM_METAL_EXTERNAL(g_pm_metal_ext_monocypher, monocypher, "4.0.2", "https://monocypher.org",
                  "crypto (trust / SSH kex)");

PM_METAL_EXTERNAL(g_pm_metal_ext_sha256, sha256, "public-domain",
                  "https://github.com/B-Con/crypto-algorithms", "SHA-256");

PM_METAL_EXTERNAL(g_pm_metal_ext_microdot, microdot, "2.6.2",
                  "https://github.com/miguelgrinberg/microdot",
                  "frozen HTTP / Inspect CORE");

/* Format-compatible Metal reimpl — not vendored liblz4. */
PM_METAL_EXTERNAL(g_pm_metal_ext_lz4, lz4, "1.9-compat",
                  "https://github.com/lz4/lz4",
                  "LZ4 block format (Metal reimpl)");

/* ---- firmware NIC / IP (not browser) ------------------------------------ */

#if !defined(PM_METAL_CFG_FW_BROWSER)

/* FreeBSD sys/dev/bge register defs (Bill Paul); no release pin in tree. */
PM_METAL_EXTERNAL(g_pm_metal_ext_freebsd_bge, freebsd_bge, "2001",
                  "https://github.com/freebsd/freebsd-src/tree/main/sys/dev/bge",
                  "if_bgereg.h (BCM57xx)");

#if defined(MICROPY_PY_LWIP) && MICROPY_PY_LWIP
#include "lwip/init.h"
PM_METAL_EXTERNAL(g_pm_metal_ext_lwip, lwip, LWIP_VERSION_STRING,
                  "https://github.com/lwip-tcpip/lwip",
                  "TCP/IP (Metal + µPy modlwip, one stack)");
PM_METAL_EXTERNAL(g_pm_metal_ext_wireguard, wireguard, "lwip-embed",
                  "https://github.com/smartalock/wireguard-lwip",
                  "WireGuard netif wgN (server+client)");
#endif

#endif /* !browser */

/* ---- UEFI Protocol headers (slim, not full EDK2 toolchain) -------------- */

#if defined(METAL_BOARD_UEFI) && METAL_BOARD_UEFI
PM_METAL_EXTERNAL(g_pm_metal_ext_edk2, edk2, "slim-headers",
                  "https://github.com/tianocore/edk2",
                  "UEFI Protocol / GUID headers only");
#endif

/* ---- TLS (browser / unix SSL seats) ------------------------------------- */

#if defined(MICROPY_SSL_MBEDTLS) && MICROPY_SSL_MBEDTLS
PM_METAL_EXTERNAL(g_pm_metal_ext_mbedtls, mbedtls, "3.6.6",
                  "https://github.com/Mbed-TLS/mbedtls", "TLS / X.509 (µPy ssl)");
#endif

/* ---- WAMR / wasmmod ----------------------------------------------------- */
/* Browser seat hosts via wasmmod (no METAL_LINK_WAMR embed) — still WAMR. */

#if (defined(METAL_LINK_WAMR) && METAL_LINK_WAMR) || defined(PM_METAL_CFG_FW_BROWSER)
PM_METAL_EXTERNAL(g_pm_metal_ext_wamr, wamr, "2.4.3",
                  "https://github.com/bytecodealliance/wasm-micro-runtime",
                  "wasm interpreter / AOT (firmware embed or browser/wasmmod host)");
PM_METAL_EXTERNAL(g_pm_metal_ext_wasmmod, wasmmod, "0.1.4-alpha",
                  "https://github.com/pymergetic/wasmmod",
                  "Metal ↔ WAMR / browser host glue");
#endif

/* Dyn fallback when linker section is empty (UEFI PE / no INSERT script). */
void pm_metal_externals_seed_fallback(void)
{
    (void)pm_metal_external_register(
        "micropython", "1.29.0", "https://github.com/micropython/micropython",
        "REPL + frozen asyncio/extmods (Metal async is separate)");
    (void)pm_metal_external_register("tlsf", "3.1", "http://tlsf.baisoku.org",
                                     "Two-Level Segregated Fit host heap");
    (void)pm_metal_external_register("monocypher", "4.0.2", "https://monocypher.org",
                                     "crypto (trust / SSH kex)");
    (void)pm_metal_external_register("sha256", "public-domain",
                                     "https://github.com/B-Con/crypto-algorithms",
                                     "SHA-256");
    (void)pm_metal_external_register("microdot", "2.6.2",
                                     "https://github.com/miguelgrinberg/microdot",
                                     "frozen HTTP / Inspect CORE");
    (void)pm_metal_external_register("lz4", "1.9-compat", "https://github.com/lz4/lz4",
                                     "LZ4 block format (Metal reimpl)");

#if !defined(PM_METAL_CFG_FW_BROWSER)
    (void)pm_metal_external_register(
        "freebsd_bge", "2001",
        "https://github.com/freebsd/freebsd-src/tree/main/sys/dev/bge",
        "if_bgereg.h (BCM57xx)");
#endif

#if defined(METAL_BOARD_UEFI) && METAL_BOARD_UEFI
    (void)pm_metal_external_register("edk2", "slim-headers",
                                     "https://github.com/tianocore/edk2",
                                     "UEFI Protocol / GUID headers only");
#endif

#if defined(MICROPY_SSL_MBEDTLS) && MICROPY_SSL_MBEDTLS
    (void)pm_metal_external_register("mbedtls", "3.6.6",
                                     "https://github.com/Mbed-TLS/mbedtls",
                                     "TLS / X.509 (µPy ssl)");
#endif

#if (defined(METAL_LINK_WAMR) && METAL_LINK_WAMR) || defined(PM_METAL_CFG_FW_BROWSER)
    (void)pm_metal_external_register(
        "wamr", "2.4.3", "https://github.com/bytecodealliance/wasm-micro-runtime",
        "wasm interpreter / AOT (firmware embed or browser/wasmmod host)");
    (void)pm_metal_external_register("wasmmod", "0.1.4-alpha",
                                     "https://github.com/pymergetic/wasmmod",
                                     "Metal ↔ WAMR / browser host glue");
#endif
}
