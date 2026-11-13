/*
 * graphics3Resolutions.c -- PrPrintABand (the C port of
 * graphics3Resolutions.asm): a band in the mode's resolution.
 */
#include "printcom.h"

Boolean PrPrintABand(PState *ps)
{
    if (ps->PS_mode <= PM_GRAPHICS_LOW_RES)
	return PrPrintLowBand(ps);
    if (ps->PS_mode <= PM_GRAPHICS_MED_RES)
	return PrPrintMediumBand(ps);
    return PrPrintHighBand(ps);
}
