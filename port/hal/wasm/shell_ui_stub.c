/*
 * Browser shell.ui — same C ABI; no gfx shadow / console viewport attach.
 */
#include "pymergetic/metal/shell/ui.h"

int pm_metal_shell_ui_attach_console0(void) { return -1; }

int pm_metal_shell_ui_present(void) { return -1; }
