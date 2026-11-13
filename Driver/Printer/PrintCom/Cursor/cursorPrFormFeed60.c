/*
 * cursorPrFormFeed60.c -- PrFormFeed (the C port of
 * cursorPrFormFeed60.asm): to the bottom of the printable area, the page
 * length set to it, the bottom margin (in 1/60", rounded) to it, a form
 * feed.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrFormFeed(PState *ps)
{
    word h = (word) (ps->PS_customHeight - ps->PS_currentMargins.PM_top -
		     ps->PS_currentMargins.PM_bottom);
    WWFixedAsDWord b = GrMulWWFixed((dword) ps->PS_currentMargins.PM_bottom
				    << 16, 54613);

    if (PrSetCursor(ps, 0, (dword) h << 16) ||
	SendCodeOut(ps, pr_codes_FormFeed) ||
	PrintStreamWriteByte(ps, (byte) ((b >> 16) + ((b >> 15) & 1))))
	return TRUE;
    return PrintStreamWriteByte(ps, C_FF);
}
