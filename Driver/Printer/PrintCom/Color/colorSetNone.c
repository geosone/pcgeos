/*
 * colorSetNone.c -- PrintSetColor of a driver that sets no colors (the C
 * port of colorSetNone.asm).
 */
#include "printcom.h"

Boolean PrintSetColor(PState *ps, DriverRegs *r)
{
    (void) ps;
    (void) r;
    return FALSE;
}
