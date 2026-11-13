/*
 * graphicsPrintSwath216.c -- PrintSwath (the C port of
 * graphicsPrintSwath216.asm): a swath of the spooler band by band, each
 * followed by the line feed to the next (1/216": 24 in all, 3 of them
 * done as scan line feeds within the band in the medium and high
 * resolutions); after a part band, the feed of its scan lines.
 */
#include "printcom.h"

/* PrintSwath: dx = the VM file, cx = the bitmap's block (Port32: the
 * VM file handle in DR_dx is 32 bits wide) */
Boolean PrintSwath(PState *ps, DriverRegs *r)
{
    word bands, left;
    sword lf;

    LoadSwathHeader(ps, (VMFileHandle) r->DR_dx, (VMBlockHandle) r->DR_cx);
    bands = PrLoadPstateVars(ps, &left);
    PrCreatePrintBuffers(ps);
    ps->PS_curColorNumber = 0;
    DerefFirstScanline(ps);
    SetFirstCMYK(ps);
    while (bands--) {
	if (PrPrintABand(ps) ||
	    PrLineFeed(ps, ps->PS_mode == PM_GRAPHICS_LOW_RES ? 24 : 21))
	    goto err;
    }
    if (left) {
	if (PrPrintABand(ps))
	    goto err;
	if (ps->PS_mode == PM_GRAPHICS_LOW_RES)
	    lf = (sword) (left * 3);
	else if (ps->PS_mode == PM_GRAPHICS_MED_RES)
	    lf = (sword) (left * 2 - 3);
	else
	    lf = (sword) (left - 3);
	if (lf > 0 && PrLineFeed(ps, (word) lf))
	    goto err;
    }
    PrDestroyPrintBuffers(ps);
    return FALSE;
err:
    PrDestroyPrintBuffers(ps);
    return TRUE;
}
