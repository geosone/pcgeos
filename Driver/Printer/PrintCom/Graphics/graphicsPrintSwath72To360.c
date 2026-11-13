/*
 * graphicsPrintSwath72To360.c -- PrintSwath (the C port of
 * graphicsPrintSwath72To360.asm): a swath of the spooler band by band,
 * each followed by the line feed to the next in 1/360" (a high
 * resolution band, a 72 dpi band in low resolution, less the single line
 * feeds of the interleaved high resolution); after a part band, the feed
 * of its scan lines.
 */
#include "printcom.h"
#include "printcomDriver.h"

/* PrintSwath: dx = the VM file, cx = the bitmap's block */
Boolean PrintSwath(PState *ps, DriverRegs *r)
{
    word bands, left, lf;

    LoadSwathHeader(ps, (VMFileHandle) r->DR_dx, (VMBlockHandle) r->DR_cx);
    bands = PrLoadPstateVars(ps, &left);
    PrCreatePrintBuffers(ps);
    ps->PS_curColorNumber = 0;
    DerefFirstScanline(ps);
    if (SetFirstCMYK(ps))
	goto err;
    while (bands--) {
	lf = HI_RES_BAND_HEIGHT;
	if (ps->PS_mode == PM_GRAPHICS_LOW_RES)
	    lf = LO_RES_BAND_HEIGHT * 5;
#if HI_RES_INTERLEAVE_FACTOR != 1
	if (ps->PS_mode == PM_GRAPHICS_HI_RES)
	    lf = (word) (lf - HI_RES_INTERLEAVE_FACTOR);
#endif
	if (PrPrintABand(ps) || PrLineFeed(ps, lf))
	    goto err;
    }
    if (left) {
	if (PrPrintABand(ps))
	    goto err;
	if (ps->PS_mode == PM_GRAPHICS_LOW_RES)
	    left = PrConvertToDriverCoordinates((WWFixedAsDWord) left << 16);
	else if (ps->PS_mode == PM_GRAPHICS_MED_RES)
	    left = (word) (left * 2);
#if HI_RES_INTERLEAVE_FACTOR != 1
	else if ((sword) (left -= HI_RES_INTERLEAVE_FACTOR) < 0)
	    left = 0;		/* the asm sent no line feed */
#endif
	if (left && PrLineFeed(ps, left))
	    goto err;
    }
    PrDestroyPrintBuffers(ps);
    return FALSE;
err:
    PrDestroyPrintBuffers(ps);
    return TRUE;
}
