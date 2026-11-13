/*
 * pscriptCursor.c -- the cursor routines (the C port of
 * pscriptCursor.asm): cx = x, dx = y in points from the top left.
 */
#include "pscript.h"

Boolean PrintSetCursor(PState *ps, DriverRegs *r)
{
    ps->PS_cursorPos.P_x = (sword) r->DR_cx;
    ps->PS_cursorPos.P_y = (sword) r->DR_dx;
    return FALSE;
}

Boolean PrintGetCursor(PState *ps, DriverRegs *r)
{
    r->DR_cx = (word) ps->PS_cursorPos.P_x;
    r->DR_dx = (word) ps->PS_cursorPos.P_y;
    return FALSE;
}
