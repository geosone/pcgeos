/*
 * cursorPCLCommon.c -- the cursor routines the PCL drivers share (the C
 * port of cursorPCLCommon.asm): both coordinates are kept in 1/300".
 */
#include "printcom.h"

/* PrintGetCursor: cx.bx = X, dx.ax = Y (points) */
Boolean PrintGetCursor(PState *ps, DriverRegs *r)
{
    WWFixedAsDWord x = PrConvertFromDriverCoordinates(
	(word) ps->PS_cursorPos.P_x);
    WWFixedAsDWord y = PrConvertFromDriverCoordinates(
	(word) ps->PS_cursorPos.P_y);

    r->DR_cx = (word) (x >> 16);
    r->DR_bx = (word) x;
    r->DR_dx = (word) (y >> 16);
    r->DR_ax = (word) y;
    return FALSE;
}

/* SendLineFeed: the cursor down by the line spacing */
Boolean SendLineFeed(PState *ps)
{
    ps->PS_cursorPos.P_y = (sword) (ps->PS_cursorPos.P_y +
				    ps->PS_asciiSpacing);
    return PrMoveCursor(ps);
}
