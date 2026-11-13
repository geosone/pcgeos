/*
 * graphicsLo8.c -- PrPrintLowBand (the C port of graphicsLo8.asm): an 8
 * scan line band in one pass (60 x 72 dpi on the Epson FX), for each
 * color plane.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrPrintLowBand(PState *ps)
{
    word start = ps->PS_newScanNumber, cols, bytes;

    for (;;) {
	PrLoadBandBuffer(ps);
	cols = PrScanBandBuffer(ps, &bytes);
	if (cols && (PrSendGraphicControlCode(ps, pr_codes_SetLoGraphics,
					      cols) ||
		     PrRotate8Lines(ps, bytes)))
	    return TRUE;
	if (SetNextCMYK(ps))
	    return TRUE;
	if (!ps->PS_curColorNumber)
	    return FALSE;
	ps->PS_newScanNumber = start;
    }
}
