/* Walk COFF .CRT$XCU — same job as ELF .init_array / emscripten libc. */
typedef void (*pm_metal_uefi_crt_fn)(void);

static pm_metal_uefi_crt_fn __attribute__((used, section(".CRT$XCA"))) s_xc_a;
static pm_metal_uefi_crt_fn __attribute__((used, section(".CRT$XCZ"))) s_xc_z;

void pm_metal_uefi_crt_init(void) {
    pm_metal_uefi_crt_fn *p;
    for (p = &s_xc_a + 1; p < &s_xc_z; p++) {
        if (*p != 0) {
            (*p)();
        }
    }
}
