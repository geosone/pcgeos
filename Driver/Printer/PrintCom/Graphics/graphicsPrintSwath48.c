/*
 * graphicsPrintSwath48.c -- PrintSwath (the C port of
 * graphicsPrintSwath48.asm): a swath of the spooler band by band, each
 * followed by a line feed of 5/48" (a band's 19/180"), Toshiba.  The asm
 * multiplied the scan lines of a last part band by 0 (GrMulWWFixed with
 * bx.ax clear), so no line feed follows it; neither does one in C.
 */
#include "printcom.h"
#include "printcomDriver.h"

/* PrintSwath: dx = the VM file, cx = the bitmap's block */
Boolean PrintSwath(PState *ps, DriverRegs *r)
{
    word bands, left;

    LoadSwathHeader(ps, (VMFileHandle) r->DR_dx, (VMBlockHandle) r->DR_cx);
    bands = PrLoadPstateVars(ps, &left);
    PrCreatePrintBuffers(ps);
    ps->PS_curColorNumber = 0;
    DerefFirstScanline(ps);
    if (SetFirstCMYK(ps))
	goto err;
    while (bands--)
	if (PrPrintABand(ps) || PrLineFeed(ps, 5))
	    goto err;
    if (left && PrPrintABand(ps))
	goto err;
    PrDestroyPrintBuffers(ps);
    return FALSE;
err:
    PrDestroyPrintBuffers(ps);
    return TRUE;
}
