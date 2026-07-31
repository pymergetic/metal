/*
 * Metal's own version tag. Build-generated from `git describe` (see
 * scripts/gen_metal_version.sh, wired into both port default.sh scripts
 * near gen_py_stubs.py) into build/pm_metal_version.inc.h — do not
 * hand-edit that file. This header just includes it (falling back to a
 * literal if the generated header is missing, e.g. a one-file clangd
 * compile before the first build) so banners/diagnostics
 * (py_shell.c, boot/authors.c) always have something to print, the same
 * way MicroPython's embedded genhdr/mpversion.h gives
 * pm_metal_py_version_cstr() (py/py.c) something to print for the
 * interpreter itself.
 */
#ifndef PYMERGETIC_METAL_VERSION_H_
#define PYMERGETIC_METAL_VERSION_H_

#if defined(__has_include)
#if __has_include("pm_metal_version.inc.h")
#include "pm_metal_version.inc.h"
#endif
#endif

#ifndef PM_METAL_VERSION
#define PM_METAL_VERSION "0.1.0-experimental"
#endif

#endif /* PYMERGETIC_METAL_VERSION_H_ */
