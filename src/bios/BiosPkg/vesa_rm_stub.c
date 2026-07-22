/** @file
  Real-mode INT bounce unavailable (e.g. long mode).
**/
#include "vesa_int10.h"

int
pm_bios_rm_int(UINT8 intno, UINT16 *ax, UINT16 *bx, UINT16 *cx, UINT16 *dx,
	       UINT16 *es, UINT16 *di, UINT16 *flags_out)
{
  (VOID)intno;
  (VOID)ax;
  (VOID)bx;
  (VOID)cx;
  (VOID)dx;
  (VOID)es;
  (VOID)di;
  (VOID)flags_out;
  return -1;
}
