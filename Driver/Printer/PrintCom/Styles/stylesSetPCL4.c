/*
 * stylesSetPCL4.c -- the styles of the PCL 4 printers (the C port of
 * stylesSetPCL4.asm): the style word kept, underline on or off; the
 * other styles are the font's (textSetFontPCL4.c, the soft fonts).
 */
#include "printcom.h"
#include "printcomDriver.h"

/* PrintSetStylesInt */
Boolean PrintSetStylesInt(PState *ps)
{
    return SendCodeOut(ps, (ps->PS_asciiStyle & PTS_UNDERLINE)
			   ? pr_codes_SetUnderline : pr_codes_ResetUnderline);
}

Boolean PrSetStyles(PState *ps, PrintTextStyle style)
{
    ps->PS_asciiStyle = style;
    return PrintSetStylesInt(ps);
}

Boolean PrintClearStyles(PState *ps)
{
    return PrSetStyles(ps, 0);
}

/* PrintSetStyles: dx = the style word */
Boolean PrintSetStyles(PState *ps, DriverRegs *r)
{
    return PrSetStyles(ps, (PrintTextStyle) r->DR_dx);
}
