/*
 * pageStart.c -- PrintStartPage (the C port of pageStart.asm): the
 * cursor home unless the form feed is suppressed, the styles cleared.
 */
#include "printcom.h"

extern Boolean PrintHomeCursor(PState *ps, DriverRegs *r);

/* PrintStartPage: cl = C_FF, or the form feed is suppressed */
Boolean PrintStartPage(PState *ps, DriverRegs *r)
{
    if ((byte) r->DR_cx == C_FF && PrintHomeCursor(ps, r))
	return TRUE;
    return PrintClearStyles(ps);
}
