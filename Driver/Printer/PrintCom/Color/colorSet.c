/*
 * colorSet.c -- PrintSetColor (the C port of colorSet.asm): on a color
 * printer the ribbon color closest to an RGB value.
 */
#include "printcom.h"

Boolean PrSetColorRGB(PState *ps, byte r, byte g, byte b)
{
    if ((ps->PS_printerType & PT_COLOR) == BMF_MONO)
	return FALSE;
    return PrSetRibbonColor(ps, PrMapRGBtoCMYKIndex(r, g, b));
}

/* PrintSetColor: al = red, dl = green, dh = blue */
Boolean PrintSetColor(PState *ps, DriverRegs *r)
{
    return PrSetColorRGB(ps, (byte) r->DR_ax, (byte) r->DR_dx,
			 (byte) (r->DR_dx >> 8));
}
