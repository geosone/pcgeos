/*
 * graphicsMed24.c -- PrPrintMediumBand (the C port of graphicsMed24.asm):
 * a band of the print head's 24 scan lines in one pass (180 x 180 dpi on
 * the Epson 24-pin), for each color plane.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrPrintMediumBand(PState *ps)
{
    word start = ps->PS_newScanNumber, cols, bytes;

    for (;;) {
	PrLoadBandBuffer(ps);
	cols = PrScanBandBuffer(ps, &bytes);
	if (cols && (PrSendGraphicControlCode(ps, pr_codes_SetMedGraphics,
					      cols) ||
		     PrRotate24Lines(ps, bytes)))
	    return TRUE;
	if (SetNextCMYK(ps))
	    return TRUE;
	if (!ps->PS_curColorNumber)
	    return FALSE;
	ps->PS_newScanNumber = start;
    }
}
