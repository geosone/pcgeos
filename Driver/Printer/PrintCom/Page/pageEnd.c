/*
 * pageEnd.c -- PrintEndPage (the C port of pageEnd.asm): the cursor's Y
 * back onto the page, the styles off, a form feed at the page's end
 * (cl = C_FF).
 */
#include "printcom.h"

Boolean PrintEndPage(PState *ps, DriverRegs *r)
{
    word page = PrConvertToDriverCoordinates((dword) ps->PS_customHeight
					     << 16);

    while ((sword) (ps->PS_cursorPos.P_y - page) >= 0)
	ps->PS_cursorPos.P_y = (sword) (ps->PS_cursorPos.P_y - page);
    if (PrintClearStyles(ps))
	return TRUE;
    if ((byte) r->DR_cx != C_FF)
	return FALSE;
    return PrFormFeed(ps);
}
