/*
 * cursor1ScanlineFeed.c -- Pr1ScanlineFeed (the C port of
 * cursor1ScanlineFeed.asm): a line feed of one driver unit.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean Pr1ScanlineFeed(PState *ps)
{
    ps->PS_cursorPos.P_y++;
    return SendCodeOut(ps, pr_codes_Do1ScanlineFeed);
}
