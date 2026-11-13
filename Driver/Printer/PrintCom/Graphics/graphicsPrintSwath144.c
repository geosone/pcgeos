/*
 * graphicsPrintSwath144.c -- PrintSwath (the C port of
 * graphicsPrintSwath144.asm): a swath of the spooler band by band, each
 * followed by the line feed to the next (1/144": a band's height, the
 * interleaves of the high and medium resolutions done as scan line feeds
 * within the band); after a part band, the feed of its scan lines.
 */
#include "printcom.h"
#include "printcomDriver.h"

/* PrintSwath: dx = the VM file, cx = the bitmap's block */
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
	    PrLineFeed(ps, (word) (HI_RES_BAND_HEIGHT -
				   HI_RES_INTERLEAVE_FACTOR +
				   (ps->PS_mode == PM_GRAPHICS_LOW_RES
				    ? HI_RES_INTERLEAVE_FACTOR : 0))))
	    goto err;
    }
    if (left) {
	if (PrPrintABand(ps))
	    goto err;
	if (ps->PS_mode == PM_GRAPHICS_LOW_RES)
	    lf = (sword) (left * 2);
	else
	    lf = (sword) (left - HI_RES_INTERLEAVE_FACTOR);
	if (lf > 0 && PrLineFeed(ps, (word) lf))
	    goto err;
    }
    PrDestroyPrintBuffers(ps);
    return FALSE;
err:
    PrDestroyPrintBuffers(ps);
    return TRUE;
}
