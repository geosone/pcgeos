/*
 * pageStartPCL4.c -- PrintStartPage of the PCL 4 printers (the C port of
 * pageStartPCL4.asm): the cursor home, the styles cleared; in the text
 * modes of a printer of soft fonts, the font manager for the page.
 */
#include "printcom.h"

extern Boolean PrintHomeCursor(PState *ps, DriverRegs *r);

Boolean PrintStartPage(PState *ps, DriverRegs *r)
{
    PrintHomeCursor(ps, r);
    if (PrSetStyles(ps, 0))
	return TRUE;
    if (ps->PS_mode < PM_FIRST_TEXT_MODE ||
	ps->PS_printerSmart == PS_DUMB_RASTER)
	return FALSE;
    return PrInitFont(ps);
}
