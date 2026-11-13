/*
 * graphics2ResHigh.c -- PrPrintABand (the C port of graphics2ResHigh.asm):
 * the low mode in the medium resolution's band, the medium and high
 * modes in the high one.
 */
#include "printcom.h"

Boolean PrPrintABand(PState *ps)
{
    if (ps->PS_mode <= PM_GRAPHICS_LOW_RES)
	return PrPrintMediumBand(ps);
    return PrPrintHighBand(ps);
}
