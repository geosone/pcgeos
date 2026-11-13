/*
 * cursorPrFormFeedIBMPPDS24.c -- PrFormFeed (the C port of
 * cursorPrFormFeedIBMPPDS24.asm): to the bottom of the printable area,
 * the form feed code with the top and bottom margins (in points, under
 * 128), a form feed.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrFormFeed(PState *ps)
{
    word h = (word) (ps->PS_customHeight - ps->PS_currentMargins.PM_top -
		     ps->PS_currentMargins.PM_bottom);

    if (PrSetCursor(ps, 0, (dword) h << 16) ||
	SendCodeOut(ps, pr_codes_FormFeed) ||
	PrintStreamWriteByte(ps, (byte) (ps->PS_currentMargins.PM_bottom +
					 ps->PS_currentMargins.PM_top)))
	return TRUE;
    return PrintStreamWriteByte(ps, C_FF);
}
