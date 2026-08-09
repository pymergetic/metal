/*
 * DOS-style autoexec: AFTER ready and AFTER mp_init.
 * Binds metal-cdn for every seat (default + optional site/iPXE/DHCP).
 */
#include "pymergetic/metal/boot/product.h"
#include "pymergetic/metal/cdn.h"

int pm_metal_autoexec(void)
{
    return pm_metal_cdn_bind();
}
