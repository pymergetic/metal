#ifndef PYMERGETIC_METAL_BIOS_VESA_INT10_H_
#define PYMERGETIC_METAL_BIOS_VESA_INT10_H_

#include "../shim/PmBiosUefi.h"

/**
 * Real-mode software interrupt bounce (i386).
 * flags_out gets post-INT FLAGS (bit 6 = ZF); may be NULL.
 */
int pm_bios_rm_int(UINT8 intno, UINT16 *ax, UINT16 *bx, UINT16 *cx, UINT16 *dx,
		   UINT16 *es, UINT16 *di, UINT16 *flags_out);

/** INT 10 (VESA) — thin wrapper over pm_bios_rm_int. */
int pm_bios_vesa_int10(UINT16 *ax, UINT16 *bx, UINT16 *cx, UINT16 *dx,
		       UINT16 *es, UINT16 *di);

#endif
