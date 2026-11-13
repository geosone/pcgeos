/*
 * graphicsHi48.c -- PrPrintHighBand (the C port of graphicsHi48.asm): a
 * band of the print head's 48 scan lines in one pass (360 x 360 dpi on
 * the Epson 48-pin), for each color plane.
 */
#include "printcom.h"
#include "printcomDriver.h"

extern Boolean PrRotate48Lines(PState *ps, word bytes);

Boolean PrPrintHighBand(PState *ps)
{
    word start = ps->PS_newScanNumber, cols, bytes;

    for (;;) {
	PrLoadBandBuffer(ps);
	cols = PrScanBandBuffer(ps, &bytes);
	if (cols && (PrSendGraphicControlCode(ps, pr_codes_SetHiGraphics,
					      cols) ||
		     PrRotate48Lines(ps, bytes)))
	    return TRUE;
	if (SetNextCMYK(ps))
	    return TRUE;
	if (!ps->PS_curColorNumber)
	    return FALSE;
	ps->PS_newScanNumber = start;
    }
}
