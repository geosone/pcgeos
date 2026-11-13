/*
 * graphicsMed8IntY.c -- PrPrintMediumBand (the C port of
 * graphicsMed8IntY.asm): a band in MED_RES_INTERLEAVE_FACTOR passes of
 * 8 lines, each pass one scan line further down, all color planes a
 * pass.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrPrintMediumBand(PState *ps)
{
    word start = ps->PS_newScanNumber, cols, bytes;
    byte n = MED_RES_INTERLEAVE_FACTOR;

    do {
	for (;;) {
	    PrLoadBandBuffer(ps);
	    cols = PrScanBandBuffer(ps, &bytes);
	    if (cols && (PrSendGraphicControlCode(ps,
						  pr_codes_SetMedGraphics,
						  cols) ||
			 PrRotate8Lines(ps, bytes)))
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
				   MED_RES_BAND_HEIGHT -
				   MED_RES_INTERLEAVE_FACTOR);
    return FALSE;
}
