/*
 * colorSetNone.c -- PrintSetColor of a driver that sets no colors (the C
 * port of colorSetNone.asm).
 */
#include "printcom.h"

/* the style runs' color (textPrintStyleRun.c): none */
Boolean PrSetColorRGB(PState *ps, byte r, byte g, byte b)
{
    (void) ps;
    (void) r;
    (void) g;
    (void) b;
    return FALSE;
}

Boolean PrintSetColor(PState *ps, DriverRegs *r)
{
    (void) ps;
    (void) r;
    return FALSE;
}
