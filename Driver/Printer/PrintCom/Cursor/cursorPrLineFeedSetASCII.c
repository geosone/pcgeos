/*
 * cursorPrLineFeedSetASCII.c -- PrLineFeed (the C port of
 * cursorPrLineFeedSetASCII.asm): line feeds of the largest spacing, then
 * one of the rest, the spacing set in two ASCII digits.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrLineFeed(PState *ps, word units)
{
    ps->PS_cursorPos.P_y = (sword) (ps->PS_cursorPos.P_y + units);
    if (units >= PR_MAX_LINE_FEED) {
	if (SendCodeOut(ps, pr_codes_SetMaxLineFeed))
	    return TRUE;
	while (units >= PR_MAX_LINE_FEED) {
	    units = (word) (units - PR_MAX_LINE_FEED);
	    if (PrintStreamWriteByte(ps, C_LF))
		return TRUE;
	}
    }
    if (!units)
	return FALSE;
    return SendCodeOut(ps, pr_codes_SetLineFeed) ||
	HexToAsciiStreamWrite0(ps, units, 2) ||
	PrintStreamWriteByte(ps, C_LF);
}
