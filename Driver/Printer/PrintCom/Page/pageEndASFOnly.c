/*
 * pageEndASFOnly.c -- PrintEndPage of the cut sheet printers (the C port
 * of pageEndASFOnly.asm): a form feed, always; in the text modes the
 * styles set to none.
 */
#include "printcom.h"

Boolean PrintEndPage(PState *ps, DriverRegs *r)
{
    (void) r;
    if (PrintStreamWriteByte(ps, C_FF))
	return TRUE;
    if (ps->PS_mode < PM_FIRST_TEXT_MODE)
	return FALSE;
    return PrSetStyles(ps, PrTestStyles(ps, 0));
}
