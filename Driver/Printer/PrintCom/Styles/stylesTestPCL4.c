/*
 * stylesTestPCL4.c -- PrintTestStyles of the PCL 4 printers (the C port of
 * stylesTestPCL4.asm): every style is legal, dx as it is.
 */
#include "printcom.h"

PrintTextStyle PrTestStyles(PState *ps, PrintTextStyle style)
{
    (void) ps;
    return style;
}

Boolean PrintTestStyles(PState *ps, DriverRegs *r)
{
    (void) ps;
    (void) r;
    return FALSE;
}
