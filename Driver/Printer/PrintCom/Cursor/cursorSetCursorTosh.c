/*
 * cursorSetCursorTosh.c -- PrSetCursor (the C port of
 * cursorSetCursorTosh.asm): Y by line feeds, X by the absolute tab to
 * the column of 10 pitch (one on, at most 125), a byte (Toshiba).
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrSetCursor(PState *ps, WWFixedAsDWord x, WWFixedAsDWord y)
{
    sword dy;
    word xi = (word) (x >> 16);
    WWFixedAsDWord cols;
    byte n;

    if (PrintClearStyles(ps))
	return TRUE;
    dy = (sword) (PrConvertToDriverCoordinates(y) -
		  (word) ps->PS_cursorPos.P_y);
    if (dy > 0) {
	ps->PS_cursorPos.P_x = (sword) 0xffff;
	if (PrLineFeed(ps, (word) dy))
	    return TRUE;
    }
    if ((word) ps->PS_cursorPos.P_x == xi)
	return FALSE;
    if ((word) ps->PS_cursorPos.P_x > xi &&
	PrintStreamWriteByte(ps, C_CR))
	return TRUE;
    ps->PS_cursorPos.P_x = (sword) xi;
    /* x .1388888: the columns of 10 pitch, rounded */
    cols = GrMulWWFixed(x, 9102);
    if (PrSetFont(ps, FID_DTC_URW_ROMAN, 12, TP_10_PITCH))
	return TRUE;
    if (!(word) ((cols >> 16) + ((cols >> 15) & 1)))
	return FALSE;
    n = (byte) ((cols >> 16) + ((cols >> 15) & 1) + 1);
    if (n >= 126)
	n = 125;
    return SendCodeOut(ps, pr_codes_AbsTab) || PrintStreamWriteByte(ps, n);
}

/* PrintSetCursor: cx.si = X, dx.ax = Y (points) */
Boolean PrintSetCursor(PState *ps, DriverRegs *r)
{
    return PrSetCursor(ps, ((dword) (word) r->DR_cx << 16) | (word) r->DR_si,
		       ((dword) (word) r->DR_dx << 16) | (word) r->DR_ax);
}
