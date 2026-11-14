/*
 * cursorPrLineFeedDumb6LPI.c -- PrLineFeed (the C port of
 * cursorPrLineFeedDumb6LPI.asm): a printer with only its fixed line feed
 * of 1/6": n units of 1/48" rounded to the nearest 8, the cursor moved by
 * them, a LF for each 8.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrLineFeed(PState *ps, word units)
{
    word n;

    if (units & 4)
	units = (word) (units + 8);
    units &= 0xfff8;
    ps->PS_cursorPos.P_y = (sword) (ps->PS_cursorPos.P_y + units);
    for (n = (word) (units >> 3); n; n--)
	if (PrintStreamWriteByte(ps, C_LF))
	    return TRUE;
    return FALSE;
}
