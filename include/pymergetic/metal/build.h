/*
 * Build personality — see README.md "Visibility".
 *
 * Define PM_METAL_BUILD_KERNEL when compiling a privileged ("kernel") mod
 * or when linking host code that should see kernel-only API declarations
 * under include/pymergetic/metal/. Ordinary mods leave it undefined; those
 * headers then hide privileged surfaces (today: guest mount/umount).
 *
 * This header itself declares nothing — it exists so the Visibility model
 * has a documented include home. Privileged headers `#ifdef
 * PM_METAL_BUILD_KERNEL` against the compile -D; they do not need to
 * #include this file (the macro is not defined here).
 *
 * impl: none — header-only (macro documentation)
 */
#ifndef PYMERGETIC_METAL_BUILD_H_
#define PYMERGETIC_METAL_BUILD_H_

#endif /* PYMERGETIC_METAL_BUILD_H_ */
