/*
 * printcomNoGraphics.c -- the graphics of a text only printer (the C port
 * of printcomNoGraphics.asm): PrintSwath does nothing.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrintSwath(PState *ps, DriverRegs *r)
{
    (void) ps;
    (void) r;
    return FALSE;
}
