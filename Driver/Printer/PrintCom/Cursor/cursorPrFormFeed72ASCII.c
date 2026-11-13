/*
 * cursorPrFormFeed72ASCII.c -- PrFormFeed (the C port of
 * cursorPrFormFeed72ASCII.asm): to the bottom of the printable area, the
 * page length code with the bottom margin as two ASCII digits, a form
 * feed.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrFormFeed(PState *ps)
{
    word h = (word) (ps->PS_customHeight - ps->PS_currentMargins.PM_top -
		     ps->PS_currentMargins.PM_bottom);

    if (PrSetCursor(ps, 0, (dword) h << 16) ||
	SendCodeOut(ps, pr_codes_FormFeed) ||
	HexToAsciiStreamWrite0(ps, ps->PS_currentMargins.PM_bottom, 2))
	return TRUE;
    return PrintStreamWriteByte(ps, C_FF);
}
