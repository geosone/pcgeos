/*
 * cursorDotMatrixCommon.c -- PrintGetCursor and SendLineFeed (the C port
 * of cursorDotMatrixCommon.asm).
 */
#include "printcom.h"

/* PrintGetCursor: cx.bx = X, dx.ax = Y (points; X is kept in points, its
 * fraction is 0) */
Boolean PrintGetCursor(PState *ps, DriverRegs *r)
{
    WWFixedAsDWord y = PrConvertFromDriverCoordinates(
	(word) ps->PS_cursorPos.P_y);

    r->DR_cx = (word) ps->PS_cursorPos.P_x;
    r->DR_bx = 0;
    r->DR_dx = (word) (y >> 16);
    r->DR_ax = (word) y;
    return FALSE;
}

/* SendLineFeed: a line feed of the line spacing */
Boolean SendLineFeed(PState *ps)
{
    return PrLineFeed(ps, ps->PS_asciiSpacing);
}
