/*
 * cursorSetCursorAbsCanonBJC.c -- PrintSetCursor (the C port of
 * cursorSetCursorAbsCanonBJC.asm): X kept (in points), a carriage return
 * to the left edge; Y forward by raster skips.
 */
#include "printcom.h"
#include "printcomDriver.h"

/* PrintSetCursor: cx.si = X, dx.ax = Y (points) */
Boolean PrintSetCursor(PState *ps, DriverRegs *r)
{
    sword dy;

    ps->PS_cursorPos.P_x = (sword) r->DR_cx;
    if (!r->DR_cx && PrintStreamWriteByte(ps, C_CR))
	return TRUE;
    dy = (sword) (PrConvertToDriverCoordinates(
		      ((dword) (word) r->DR_dx << 16) | (word) r->DR_ax) -
		  (word) ps->PS_cursorPos.P_y);
    return dy > 0 ? PrLineFeed(ps, (word) dy) : FALSE;
}
