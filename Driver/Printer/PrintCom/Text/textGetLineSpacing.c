/*
 * textGetLineSpacing.c -- PrintGetLineSpacing (the C port of
 * textGetLineSpacing.asm): dx.ax = the line spacing in points.
 */
#include "printcom.h"

Boolean PrintGetLineSpacing(PState *ps, DriverRegs *r)
{
    WWFixedAsDWord s = PrConvertFromDriverCoordinates(ps->PS_asciiSpacing);

    r->DR_dx = (word) (s >> 16);
    r->DR_ax = (word) s;
    return FALSE;
}
