/*
 * cursorSetCursorAbs72.c -- PrintSetCursor (the C port of
 * cursorSetCursorAbs72.asm): Y in driver units by line feeds (down
 * only), X absolute in 1/60" (PR_X_CURSOR: 60/72).
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrSetCursor(PState *ps, WWFixedAsDWord x, WWFixedAsDWord y)
{
    sword dy;
    WWFixedAsDWord pos;
    word n;

    if (PrintClearStyles(ps))
	return TRUE;
    dy = (sword) (PrConvertToDriverCoordinates(y) -
		  (word) ps->PS_cursorPos.P_y);
    if (dy > 0 && PrLineFeed(ps, (word) dy))
	return TRUE;
    ps->PS_cursorPos.P_x = (sword) (x >> 16);
    pos = GrMulWWFixed(x, ((dword) PR_X_CURSOR_INT << 16) |
		       PR_X_CURSOR_FRAC);
    n = (word) ((pos >> 16) + ((pos >> 15) & 1));	/* rounded */
    if (SendCodeOut(ps, pr_codes_AbsPos) ||
	PrintStreamWriteByte(ps, (byte) n))
	return TRUE;
    return PrintStreamWriteByte(ps, (byte) (n >> 8));
}

/* PrintSetCursor: cx.si = X, dx.ax = Y (points) */
Boolean PrintSetCursor(PState *ps, DriverRegs *r)
{
    return PrSetCursor(ps, ((dword) (word) r->DR_cx << 16) | (word) r->DR_si,
		       ((dword) (word) r->DR_dx << 16) | (word) r->DR_ax);
}
