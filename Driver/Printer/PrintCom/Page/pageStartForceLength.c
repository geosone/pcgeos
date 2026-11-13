/*
 * pageStartForceLength.c -- PrintStartPage (the C port of
 * pageStartForceLength.asm): the paper length set on every path, the
 * cursor home on a new page (cl = C_FF), the styles off.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrintStartPage(PState *ps, DriverRegs *r)
{
    if (SendCodeOut(ps, pr_codes_InitPaperLength))
	return TRUE;
    if ((byte) r->DR_cx == C_FF) {
	ps->PS_cursorPos.P_x = 0;		/* PrintHomeCursor */
	ps->PS_cursorPos.P_y = 0;
    }
    return PrintClearStyles(ps);
}
