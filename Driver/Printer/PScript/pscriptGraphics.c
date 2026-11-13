/*
 * pscriptGraphics.c -- PrintSwath (the C port of pscriptGraphics.asm).
 *
 * The asm drew the swath (a huge bitmap) through the library's
 * EPSExportBitmap, which is not ported yet (Port32/PLAN.md); until it
 * is, a swath is an error.  The spooler sends PDL printers gstrings
 * (DR_PRINT_ESC_PRINT_GSTRING), not swaths.
 */
#include "pscript.h"

Boolean PrintSwath(PState *ps, DriverRegs *r)
{
    (void) r;
    ps->PS_error = 1;
    return TRUE;
}
