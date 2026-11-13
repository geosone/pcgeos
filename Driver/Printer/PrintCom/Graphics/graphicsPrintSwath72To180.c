/*
 * graphicsPrintSwath72To180.c -- PrintSwath (the C port of
 * graphicsPrintSwath72To180.asm): a swath of the spooler band by band,
 * each followed by the line feed of a high resolution band in 1/180"
 * (the same height in all three resolutions); after a part band, the
 * feed of its scan lines (72 dpi ones converted).
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
	if (PrPrintABand(ps) || PrLineFeed(ps, HI_RES_BAND_HEIGHT))
	    goto err;
    if (left) {
	if (PrPrintABand(ps))
	    goto err;
	if (ps->PS_mode == PM_GRAPHICS_LOW_RES)
	    left = PrConvertToDriverCoordinates((WWFixedAsDWord) left << 16);
	if (PrLineFeed(ps, left))
	    goto err;
    }
    PrDestroyPrintBuffers(ps);
    return FALSE;
err:
    PrDestroyPrintBuffers(ps);
    return TRUE;
}
