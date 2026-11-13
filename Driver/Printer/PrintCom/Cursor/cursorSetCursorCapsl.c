/*
 * cursorSetCursorCapsl.c -- PrintSetCursor and PrMoveCursor (the C port
 * of cursorSetCursorCapsl.asm): the cursor in 1/300", absolute, CSI y ; x
 * f.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrMoveCursor(PState *ps)
{
    return SendCodeOut(ps, pr_codes_CSIcode) ||
	HexToAsciiStreamWrite(ps, (word) ps->PS_cursorPos.P_y) ||
	PrintStreamWriteByte(ps, ';') ||
	HexToAsciiStreamWrite(ps, (word) ps->PS_cursorPos.P_x) ||
	PrintStreamWriteByte(ps, 'f');
}

Boolean PrSetCursor(PState *ps, WWFixedAsDWord x, WWFixedAsDWord y)
{
    ps->PS_cursorPos.P_y = (sword) PrConvertToDriverCoordinates(y);
    ps->PS_cursorPos.P_x = (sword) PrConvertToDriverCoordinates(x);
    return PrMoveCursor(ps);
}

/* PrintSetCursor: cx.si = X, dx.ax = Y (points) */
Boolean PrintSetCursor(PState *ps, DriverRegs *r)
{
    return PrSetCursor(ps, ((dword) (word) r->DR_cx << 16) | (word) r->DR_si,
		       ((dword) (word) r->DR_dx << 16) | (word) r->DR_ax);
}
