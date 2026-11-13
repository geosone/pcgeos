/*
 * graphicsLo7.c -- PrPrintLowBand (the C port of graphicsLo7.asm): a band
 * of 7 scan lines between the codes that enter and leave graphics
 * (Okidata), for each color plane.
 */
#include "printcom.h"
#include "printcomDriver.h"

extern Boolean PrRotate7Lines(PState *ps, word bytes);

Boolean PrPrintLowBand(PState *ps)
{
    word start = ps->PS_newScanNumber, bytes;

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
	    return FALSE;
	ps->PS_newScanNumber = start;
    }
}
