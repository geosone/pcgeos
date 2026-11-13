/*
 * cursorSetCursorTab72ASCII.c -- PrSetCursor (the C port of
 * cursorSetCursorTab72ASCII.asm): Y by line feeds, X by a tab to the
 * column of 10 pitch, the column as three ASCII digits (Okidata).
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrSetCursor(PState *ps, WWFixedAsDWord x, WWFixedAsDWord y)
{
    sword dy;
    word xi = (word) (x >> 16);
    WWFixedAsDWord cols;
    word n;

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
    n = (word) ((cols >> 16) + ((cols >> 15) & 1));
    if (PrSetFont(ps, FID_DTC_URW_ROMAN, 12, TP_10_PITCH))
	return TRUE;
    return SendCodeOut(ps, pr_codes_SetTab) ||
	HexToAsciiStreamWrite0(ps, n, 3) ||
	SendCodeOut(ps, pr_codes_DoTab);
}

/* PrintSetCursor: cx.si = X, dx.ax = Y (points) */
Boolean PrintSetCursor(PState *ps, DriverRegs *r)
{
    return PrSetCursor(ps, ((dword) (word) r->DR_cx << 16) | (word) r->DR_si,
		       ((dword) (word) r->DR_dx << 16) | (word) r->DR_ax);
}
