/*
 * pageEndPCL4.c -- PrintEndPage of the PCL 4 printers (the C port of
 * pageEndPCL4.asm): a form feed; in the text modes of a printer of soft
 * fonts the styles cleared and the page's soft fonts deleted.
 */
#include "printcom.h"

Boolean PrintEndPage(PState *ps, DriverRegs *r)
{
    (void) r;
    if (PrintStreamWriteByte(ps, C_FF))
	return TRUE;
    if (ps->PS_printerSmart == PS_DUMB_RASTER ||
	ps->PS_mode < PM_FIRST_TEXT_MODE)
	return FALSE;
    return PrSetStyles(ps, 0) || FontExit(ps);
}
