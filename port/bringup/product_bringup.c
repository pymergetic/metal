#include "product_bringup.h"

#include "pymergetic/metal/boot/product.h"

int pm_metal_product_bringup(void)
{
    /* Boot only — CDN autoexec runs after mp_init (main_upy / browser). */
    return pm_metal_boot();
}
