/*
 * graphicsHi7IntY.c -- PrPrintHighBand (the C port of
 * graphicsHi7IntY.asm): a band in HI_RES_INTERLEAVE_FACTOR passes of 7
 * scan lines between the codes that enter and leave graphics (Okidata),
 * each pass one scan line further down, all color planes a pass.
 */
#include "printcom.h"
#include "printcomDriver.h"

extern Boolean PrRotate7Lines(PState *ps, word bytes);

Boolean PrPrintHighBand(PState *ps)
{
    word start = ps->PS_newScanNumber, bytes;
    byte n = HI_RES_INTERLEAVE_FACTOR;

    do {
	for (;;) {
	    PrLoadBandBuffer(ps);
	    if (PrScanBandBuffer(ps, &bytes) &&
		(SendCodeOut(ps, pr_codes_EnterGraphics) ||
		 PrRotate7Lines(ps, bytes) ||
		 SendCodeOut(ps, pr_codes_ExitGraphics)))
		return TRUE;
	    if (SetNextCMYK(ps))
		return TRUE;
	    if (!ps->PS_curColorNumber)
		break;
	    ps->PS_newScanNumber = start;
	}
	start++;
	ps->PS_newScanNumber = start;
	if (Pr1ScanlineFeed(ps))
	    return TRUE;
    } while (--n);
    ps->PS_newScanNumber = (word) (ps->PS_newScanNumber +
				   HI_RES_BAND_HEIGHT -
				   HI_RES_INTERLEAVE_FACTOR);
    return FALSE;
}
