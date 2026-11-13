/*
 * graphicsHi24IntXY.c -- PrPrintHighBand (the C port of
 * graphicsHi24IntXY.asm): a band in HI_RES_INTERLEAVE_FACTOR passes of 24
 * scan lines, each pass the even and the odd columns (rotate2pass24.c),
 * then a single line feed (1/360") to the next pass.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrPrintHighBand(PState *ps)
{
    word start = ps->PS_newScanNumber;
    byte n = HI_RES_INTERLEAVE_FACTOR;

    do {
	if (PrSend24HiresLines(ps, &start) ||
	    SendCodeOut(ps, pr_codes_DoSingleLineFeed))
	    return TRUE;
	ps->PS_cursorPos.P_y++;
	start++;
	ps->PS_newScanNumber = start;
    } while (--n);
    ps->PS_newScanNumber = (word) (ps->PS_newScanNumber +
				   HI_RES_BAND_HEIGHT -
				   HI_RES_INTERLEAVE_FACTOR);
    return FALSE;
}
