/*
 * cursorPrLineFeedExe.c -- PrLineFeed (the C port of
 * cursorPrLineFeedExe.asm): a line feed of n driver units, in pieces of
 * PR_MAX_LINE_FEED.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrLineFeed(PState *ps, word units)
{
    ps->PS_cursorPos.P_y = (sword) (ps->PS_cursorPos.P_y + units);
    while ((sword) (units - PR_MAX_LINE_FEED) > 0) {
	units = (word) (units - PR_MAX_LINE_FEED);
	if (SendCodeOut(ps, pr_codes_DoMaxLineFeed))
	    return TRUE;
    }
    /* Port32: nothing for 0; the asm sent the code without its count
     * byte, so the next byte the printer got became it */
    if (!units)
	return FALSE;
    if (SendCodeOut(ps, pr_codes_DoLineFeed))
	return TRUE;
    return PrintStreamWriteByte(ps, (byte) units);
}
