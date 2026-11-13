/*
 * stylesGetPCL4.c -- PrintGetStyles of the PCL 4 printers (the C port of
 * stylesGetPCL4.asm): cx = PS_asciiStyle.
 */
#include "printcom.h"

Boolean PrintGetStyles(PState *ps, DriverRegs *r)
{
    r->DR_cx = ps->PS_asciiStyle;
    return FALSE;
}
