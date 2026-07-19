/*
 * qemu-intel64 boardinit.c references romfs_img / romfs_img_len. Upstream
 * Makefile links romfs_stub.c, but the board CMakeLists.txt does not — so
 * flat cmake builds fail at link. Provide the same empty stub from Metal
 * when building for this board (NSECTORS(len)==0 → late_init skips register).
 */
#include <nuttx/config.h>

#if defined(CONFIG_ARCH_BOARD_INTEL64_QEMU)

const unsigned char romfs_img[] __attribute__((aligned(4))) = {0x00};
const unsigned int romfs_img_len = 1;

#endif /* CONFIG_ARCH_BOARD_INTEL64_QEMU */
