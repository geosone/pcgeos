/*
 * colorSetFirstCMYK.c -- SetFirstCMYK (the C port of
 * colorSetFirstCMYK.asm): for a CMYK swath the first ribbon color.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean SetFirstCMYK(PState *ps)
{
    if ((ps->PS_swath.B_type & BMT_FORMAT) != BMF_4CMYK)
	return FALSE;
    return SendCodeOut(ps, pr_codes_SetYellow);
}
