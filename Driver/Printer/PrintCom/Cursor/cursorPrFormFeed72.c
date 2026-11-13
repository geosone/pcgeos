/*
 * cursorPrFormFeed72.c -- PrFormFeed (the C port of
 * cursorPrFormFeed72.asm): to the bottom of the page, then the page
 * length set to the bottom margin and a form feed.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrFormFeed(PState *ps)
{
    word h = (word) (ps->PS_customHeight - ps->PS_currentMargins.PM_top -
		     ps->PS_currentMargins.PM_bottom);

    if (PrSetCursor(ps, 0, (dword) h << 16) ||
	SendCodeOut(ps, pr_codes_FormFeed) ||
	PrintStreamWriteByte(ps, (byte) ps->PS_currentMargins.PM_bottom))
	return TRUE;
    return PrintStreamWriteByte(ps, C_FF);
}
