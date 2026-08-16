/* Walk ELF .init_array — same job as UEFI .CRT$XCU and emscripten libc. */
typedef void (*pm_metal_ctor_fn)(void);

extern pm_metal_ctor_fn __init_array_start[];
extern pm_metal_ctor_fn __init_array_end[];

void pm_metal_elf_ctors_run(void) {
    pm_metal_ctor_fn *p;
    for (p = __init_array_start; p < __init_array_end; p++) {
        if (*p != 0) {
            (*p)();
        }
    }
}
