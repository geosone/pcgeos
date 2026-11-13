/*
 * colorSetRibbon.c -- PrSetRibbonColor (the C port of
 * colorSetRibbon.asm): the code to select a ribbon color, then its
 * number.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrSetRibbonColor(PState *ps, byte color)
{
    if (SendCodeOut(ps, pr_codes_SetColor))
	return TRUE;
    return PrintStreamWriteByte(ps, color);
}
