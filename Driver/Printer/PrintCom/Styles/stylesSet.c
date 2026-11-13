/*
 * stylesSet.c -- PrintSetStyles and PrintClearStyles (the C port of
 * stylesSet.asm): each style bit that changes, from the highest, is set
 * or reset by its routine (printSetStyle, printResetStyle of the
 * driver's styles composite).
 */
#include "printcom.h"

/* PrintSetStylesInt */
Boolean PrSetStyles(PState *ps, PrintTextStyle style)
{
    word changed = (word) (ps->PS_asciiStyle ^ style);
    int bit;

    ps->PS_asciiStyle = style;
    for (bit = 15; bit >= 0; bit--)
	if ((changed & (1 << bit)) &&
	    ((style & (1 << bit)) ? printSetStyle[bit](ps)
				   : printResetStyle[bit](ps)))
	    return TRUE;
    return FALSE;
}

Boolean PrintClearStyles(PState *ps)
{
    return PrSetStyles(ps, 0);
}

/* PrintSetStyles: dx = the style word, tested first (PrintTestStyles,
 * which returns it legalized in dx) */
Boolean PrintSetStyles(PState *ps, DriverRegs *r)
{
    r->DR_dx = PrTestStyles(ps, (PrintTextStyle) r->DR_dx);
    return PrSetStyles(ps, (PrintTextStyle) r->DR_dx);
}
