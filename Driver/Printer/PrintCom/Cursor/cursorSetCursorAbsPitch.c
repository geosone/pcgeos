/*
 * cursorSetCursorAbsPitch.c -- PrSetCursor (the C port of
 * cursorSetCursorAbsPitch.asm): Y by line feeds, X absolute in 1/160"
 * as four ASCII digits after the proportional font (C.Itoh).
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrSetCursor(PState *ps, WWFixedAsDWord x, WWFixedAsDWord y)
{
    sword dy;

    if (PrintClearStyles(ps))
	return TRUE;
    dy = (sword) (PrConvertToDriverCoordinates(y) -
		  (word) ps->PS_cursorPos.P_y);
    if (dy > 0 && PrLineFeed(ps, (word) dy))
	return TRUE;
    ps->PS_cursorPos.P_x = (sword) (word) (x >> 16);
    if (PrSetFont(ps, FID_DTC_URW_ROMAN, 12, TP_PROPORTIONAL))
	return TRUE;
    /* x 2.2222: 1/160" */
    return SendCodeOut(ps, pr_codes_AbsPos) ||
	HexToAsciiStreamWrite0(ps, (word) (GrMulWWFixed(x, 0x238e4UL) >> 16),
			       4);
}

/* PrintSetCursor: cx.si = X, dx.ax = Y (points) */
Boolean PrintSetCursor(PState *ps, DriverRegs *r)
{
    return PrSetCursor(ps, ((dword) (word) r->DR_cx << 16) | (word) r->DR_si,
		       ((dword) (word) r->DR_dx << 16) | (word) r->DR_ax);
}
