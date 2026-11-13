/*
 * cursorSetCursorSpace72.c -- PrintSetCursor (the C port of
 * cursorSetCursorSpace72.asm): Y in driver units by line feeds (down
 * only; then the head is at the left), X by spaces of 10 pitch (12 pitch
 * if that is the font's) from where the head is, a carriage return first
 * when it has to go left.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrSetCursor(PState *ps, WWFixedAsDWord x, WWFixedAsDWord y)
{
    sword dy;
    word xi = (word) (x >> 16), old = (word) ps->PS_cursorPos.P_x;
    WWFixedAsDWord cols;
    byte pitch;
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
    if ((word) ps->PS_cursorPos.P_x > xi) {
	old = 0;
	if (PrintStreamWriteByte(ps, C_CR))
	    return TRUE;
    }
    ps->PS_cursorPos.P_x = (sword) xi;
    /* the points to go as spaces: x .1388888 (10 pitch) or x .1666666 */
    pitch = ps->PS_curFont.FE_pitch == TP_12_PITCH ? TP_12_PITCH
						   : TP_10_PITCH;
    cols = GrMulWWFixed(((dword) (word) (xi - old) << 16) | (word) x,
			pitch == TP_12_PITCH ? 10923 : 9102);
    n = (word) ((cols >> 16) + ((cols >> 15) & 1));
    if (PrSetFont(ps, FID_DTC_URW_ROMAN, 12, pitch))
	return TRUE;
    while (n--)
	if (PrintStreamWriteByte(ps, C_SPACE))
	    return TRUE;
    return FALSE;
}

/* PrintSetCursor: cx.si = X, dx.ax = Y (points) */
Boolean PrintSetCursor(PState *ps, DriverRegs *r)
{
    return PrSetCursor(ps, ((dword) (word) r->DR_cx << 16) | (word) r->DR_si,
		       ((dword) (word) r->DR_dx << 16) | (word) r->DR_ax);
}
