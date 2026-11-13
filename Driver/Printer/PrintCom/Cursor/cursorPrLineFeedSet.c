/*
 * cursorPrLineFeedSet.c -- PrLineFeed (the C port of
 * cursorPrLineFeedSet.asm): a line feed of n driver units by line feed
 * characters: the spacing set to PR_MAX_LINE_FEED for as many of those
 * as fit, then to the rest for one more.
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
	PrintStreamWriteByte(ps, (byte) units) ||
	PrintStreamWriteByte(ps, C_LF);
}
