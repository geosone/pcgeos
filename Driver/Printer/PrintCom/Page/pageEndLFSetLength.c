/*
 * pageEndLFSetLength.c -- PrintEndPage (the C port of
 * pageEndLFSetLength.asm): to the top of the next page, by a form feed
 * on cut sheets, by line feeds on tractor paper; with the form feed
 * suppressed, text modes go a line down.
 */
#include "printcom.h"
#include "printcomDriver.h"

/* PrintEndPage: cl = C_FF, or the form feed is suppressed */
Boolean PrintEndPage(PState *ps, DriverRegs *r)
{
    word page = PrConvertToDriverCoordinates((dword) ps->PS_customHeight
					     << 16);
    Boolean err;

    while ((sword) (ps->PS_cursorPos.P_y - page) >= 0)
	ps->PS_cursorPos.P_y = (sword) (ps->PS_cursorPos.P_y - page);
    if ((byte) r->DR_cx == C_FF) {
	if (!(ps->PS_paperInput & PIO_TRACTOR))
	    err = PrFormFeed(ps);
	else
	    err = PrLineFeed(ps, (word) (page - ps->PS_cursorPos.P_y));
	if (err)
	    return TRUE;
	return PrintClearStyles(ps);
    }
    if (ps->PS_mode < PM_FIRST_TEXT_MODE)
	return FALSE;
    return PrSetCursor(ps, 0, (dword) (word) (ps->PS_curFont.FE_size +
					     ps->PS_cursorPos.P_y) << 16);
}
