/*
 * graphicsHi8IntXY.c -- PrPrintHighBand (the C port of
 * graphicsHi8IntXY.asm): a 24 scan line band in HI_RES_INTERLEAVE_FACTOR
 * passes of 8 lines, each 1/216" below the last, each in even and odd
 * columns (240 x 216 dpi on the Epson FX).
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrPrintHighBand(PState *ps)
{
    word start = ps->PS_newScanNumber;
    int pass;

    for (pass = HI_RES_INTERLEAVE_FACTOR; pass > 0; pass--) {
	if (PrSend8HiresLines(ps, &start))
	    return TRUE;
	start++;
	ps->PS_newScanNumber = start;
	if (Pr1ScanlineFeed(ps))
	    return TRUE;
    }
    ps->PS_newScanNumber = (word) (ps->PS_newScanNumber +
				   HI_RES_BAND_HEIGHT -
				   HI_RES_INTERLEAVE_FACTOR);
    return FALSE;
}
