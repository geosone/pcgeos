/*
 * graphicsMed8Int3Y.c -- PrPrintMediumBand (the C port of
 * graphicsMed8Int3Y.asm): a 12 scan line band in three passes of 4
 * lines 3 apart (120 x 108 dpi on the Epson 9-pin), the second and third
 * one 1/216" further down, every other pin left out.
 */
#include "printcom.h"
#include "printcomDriver.h"

/* one pass of all color planes from scan line start */
static Boolean Pass(PState *ps, word start, Boolean zeroTop)
{
    word cols, bytes;

    ps->PS_newScanNumber = start;
    for (;;) {
	PrLoadBandBuffer(ps);
	cols = PrScanBandBuffer(ps, &bytes);
	if (cols && (PrSendGraphicControlCode(ps, pr_codes_SetMedGraphics,
					      cols) ||
		     (zeroTop ? PrRotate4LinesZeroTop(ps, bytes)
			      : PrRotate4LinesZeroBottom(ps, bytes))))
	    return TRUE;
	if (SetNextCMYK(ps))
	    return TRUE;
	if (!ps->PS_curColorNumber)
	    return FALSE;
	ps->PS_newScanNumber = start;
    }
}

Boolean PrPrintMediumBand(PState *ps)
{
    word start = ps->PS_newScanNumber;

    if (Pass(ps, start, FALSE))
	return TRUE;
    start = (word) (start + 2);
    ps->PS_newScanNumber = start;
    if (Pr1ScanlineFeed(ps) || Pass(ps, start, TRUE))
	return TRUE;
    start--;
    ps->PS_newScanNumber = start;
    if (Pr1ScanlineFeed(ps) || Pass(ps, start, FALSE))
	return TRUE;
    ps->PS_newScanNumber--;
    return Pr1ScanlineFeed(ps);
}
