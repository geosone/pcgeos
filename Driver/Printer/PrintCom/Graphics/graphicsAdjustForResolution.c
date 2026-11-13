/*
 * graphicsAdjustForResolution.c -- dots of a graphics mode in 1/300" (the
 * C port of graphicsAdjustForResolution.asm): times 1, 2 or 4.
 */
#include "printcom.h"

word PrAdjustForResolution(PState *ps, word dots)
{
    if (ps->PS_mode == PM_GRAPHICS_HI_RES)
	return dots;
    dots = (word) (dots << 1);
    if (ps->PS_mode == PM_GRAPHICS_MED_RES)
	return dots;
    return (word) (dots << 1);
}
