/*
 * stylesGet.c -- PrintGetStyles (the C port of stylesGet.asm): cx = the
 * style bits set.
 */
#include "printcom.h"

Boolean PrintGetStyles(PState *ps, DriverRegs *r)
{
    r->DR_cx = ps->PS_asciiStyle;
    return FALSE;
}
