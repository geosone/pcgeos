/*
 * cursorPrLineFeedCanonBJC.c -- PrLineFeed (the C port of
 * cursorPrLineFeedCanonBJC.asm): a raster skip of the lines, high byte
 * first.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrLineFeed(PState *ps, word units)
{
    ps->PS_cursorPos.P_y = (sword) (ps->PS_cursorPos.P_y + units);
    return SendCodeOut(ps, pr_codes_DoLineFeed) ||
	PrintStreamWriteByte(ps, (byte) (units >> 8)) ||
	PrintStreamWriteByte(ps, (byte) units);
}
