/** @file MicroPython lib/libm/math.c without clashing WAMR / Metal helpers.
 *
 * WAMR already exports floorf/ceilf/truncf (and sqrtf). Metal py_libm_extra
 * always provides copysignf (µPy's is NDEBUG-gated and races us when NDEBUG
 * is unset). Rename those at include time; keep logf/expf/powf/sinhf/….
 */
#define floorf    __metal_upy_unused_floorf
#define ceilf     __metal_upy_unused_ceilf
#define truncf    __metal_upy_unused_truncf
#define copysignf __metal_upy_unused_copysignf
/* Repo-relative path so clangd finds it without depending on -I order;
 * EDK2/bios builds also resolve quoted includes from this file's directory. */
#include "../../../../external/micropython/lib/libm/math.c"
