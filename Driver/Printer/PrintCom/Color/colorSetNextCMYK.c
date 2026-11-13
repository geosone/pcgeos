/*
 * colorSetNextCMYK.c -- SetNextCMYK (the C port of colorSetNextCMYK.asm):
 * for a CMYK swath the next color plane (PS_curColorNumber, back to 0
 * after the fourth) and its ribbon color.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean SetNextCMYK(PState *ps)
{
    static const byte *const colorCodes[4] = {
	pr_codes_SetYellow, pr_codes_SetCyan, pr_codes_SetMagenta,
	pr_codes_SetBlack
    };

    if ((ps->PS_swath.B_type & BMT_FORMAT) != BMF_4CMYK)
	return FALSE;
    ps->PS_curColorNumber = (word) ((ps->PS_curColorNumber + 1) & 3);
    return SendCodeOut(ps, colorCodes[ps->PS_curColorNumber]);
}
