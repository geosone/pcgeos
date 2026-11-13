/*
 * stylesTest.c -- PrintTestStyles (the C port of stylesTest.asm): the
 * style word made legal for the font (FE_styles), with its mandatory
 * styles (FE_stylesSet) and NLQ in the NLQ mode.
 */
#include "printcom.h"

PrintTextStyle PrTestStyles(PState *ps, PrintTextStyle style)
{
    if (ps->PS_mode == PM_TEXT_NLQ)
	style |= PTS_NLQ;
    return (PrintTextStyle) ((style & ps->PS_curFont.FE_styles) |
			     ps->PS_curFont.FE_stylesSet);
}

/* PrintTestStyles: dx = the style word, legalized */
Boolean PrintTestStyles(PState *ps, DriverRegs *r)
{
    r->DR_dx = PrTestStyles(ps, (PrintTextStyle) r->DR_dx);
    return FALSE;
}
