/*
 * pscriptPage.c -- the page routines (the C port of pscriptPage.asm).
 */
#include "pscript.h"

static TransError CallPage(PState *ps, word routine)
{
    EPSExportOptStreamFn *f = (EPSExportOptStreamFn *)
	CallEPSLibrary(ps, routine);

    return f ? f(ps->PS_expansionInfo, PSStreamBlock(ps))
	     : TE_EXPORT_ERROR;
}

/* PrintStartPage: the cursor home, then the library's page setup */
Boolean PrintStartPage(PState *ps, DriverRegs *r)
{
    (void) r;
    ps->PS_cursorPos.P_x = 0;
    ps->PS_cursorPos.P_y = 0;
    return CallPage(ps, TR_EXPORT_BEGIN_PAGE) != TE_NO_ERROR;
}

/* PrintEndPage: the carry is clear, as in the asm */
Boolean PrintEndPage(PState *ps, DriverRegs *r)
{
    (void) r;
    if (CallPage(ps, TR_EXPORT_END_PAGE) != TE_NO_ERROR)
	ps->PS_error = 1;
    return FALSE;
}
