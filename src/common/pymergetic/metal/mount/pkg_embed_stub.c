/*
 * Empty guest-package init — linked only when build/guest-pkgs is absent.
 * Prefer scripts/lib/guest-pkgs.sh (pkgs_init.c) for real embeds.
 */
#include "pymergetic/metal/mount/pkg.h"

void pm_metal_pkg_embed_init(void)
{
}
