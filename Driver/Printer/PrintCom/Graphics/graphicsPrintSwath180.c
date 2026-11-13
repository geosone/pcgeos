/*
 * graphicsPrintSwath180.c -- PrintSwath (the C port of
 * graphicsPrintSwath180.asm): a swath of the spooler band by band, each
 * followed by the line feed to the next (1/180": HI_RES_BAND_HEIGHT);
 * after a part band, the feed of its scan lines (three units a line in
 * the low resolution of 60 dpi).
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
    SetFirstCMYK(ps);
    while (bands--)
	if (PrPrintABand(ps) || PrLineFeed(ps, HI_RES_BAND_HEIGHT))
	    goto err;
    if (left) {
	if (PrPrintABand(ps))
	    goto err;
	if (ps->PS_mode == PM_GRAPHICS_LOW_RES)
	    left = (word) (left * 3);
	if (PrLineFeed(ps, left))
	    goto err;
    }
    PrDestroyPrintBuffers(ps);
    return FALSE;
err:
    PrDestroyPrintBuffers(ps);
    return TRUE;
}
