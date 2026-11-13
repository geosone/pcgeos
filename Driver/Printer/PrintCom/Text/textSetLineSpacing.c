/*
 * textSetLineSpacing.c -- PrintSetLineSpacing (the C port of
 * textSetLineSpacing.asm): dx.ax = the line spacing in points.
 */
#include "printcom.h"

Boolean PrintSetLineSpacing(PState *ps, DriverRegs *r)
{
    ps->PS_asciiSpacing = PrConvertToDriverCoordinates(
	((dword) (word) r->DR_dx << 16) | (word) r->DR_ax);
    return FALSE;
}
