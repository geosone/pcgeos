/*
 * cursorSetCursorPCL.c -- the absolute cursor of the PCL drivers (the C
 * port of cursorSetCursorPCL.asm): ESC *p <x> x <y> Y in 1/300", the
 * styles off around it.
 */
#include "printcom.h"
#include "printcomDriver.h"

/* PrMoveCursor: to PS_cursorPos */
Boolean PrMoveCursor(PState *ps)
{
    PrintTextStyle style = ps->PS_asciiStyle;

    if (PrintClearStyles(ps) ||
	SendCodeOut(ps, pr_codes_CursorPosition) ||
	HexToAsciiStreamWrite(ps, (word) ps->PS_cursorPos.P_x) ||
	PrintStreamWriteByte(ps, 'x') ||
	HexToAsciiStreamWrite(ps, (word) ps->PS_cursorPos.P_y) ||
	PrintStreamWriteByte(ps, 'Y'))
	return TRUE;
    return PrSetStyles(ps, PrTestStyles(ps, style));
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
