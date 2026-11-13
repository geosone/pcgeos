/*
 * pageStartSetLength.c -- PrintStartPage (the C port of
 * pageStartSetLength.asm): on cut sheets the page length to the
 * maximum, then the cursor home (unless the form feed is suppressed)
 * and the styles off.
 */
#include "printcom.h"
#include "printcomDriver.h"

/* PrintStartPage: cl = C_FF, or the form feed is suppressed */
Boolean PrintStartPage(PState *ps, DriverRegs *r)
{
    if (!(ps->PS_paperInput & PIO_TRACTOR) &&
	SendCodeOut(ps, pr_codes_InitPaperLength))
	return TRUE;
    if ((byte) r->DR_cx == C_FF) {
	ps->PS_cursorPos.P_x = 0;		/* PrintHomeCursor */
	ps->PS_cursorPos.P_y = 0;
    }
    return PrintClearStyles(ps);
}
