/*
 * graphics2Resolutions.c -- PrPrintABand (the C port of
 * graphics2Resolutions.asm): a band in the low or the high resolution
 * (the medium mode prints in the high one).
 */
#include "printcom.h"

Boolean PrPrintABand(PState *ps)
{
    if (ps->PS_mode <= PM_GRAPHICS_LOW_RES)
	return PrPrintLowBand(ps);
    return PrPrintHighBand(ps);
}
