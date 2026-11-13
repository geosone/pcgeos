/*
 * printcomNoStyles.c -- the style routines of a driver without styles
 * (the C port of printcomNoStyles.asm).
 */
#include "printcom.h"

/* PrintGetStyles: no styles, cx 0 */
Boolean PrintGetStyles(PState *ps, DriverRegs *r)
{
    (void) ps;
    r->DR_cx = 0;
    return FALSE;
}

Boolean PrintSetStyles(PState *ps, DriverRegs *r)
{
    (void) ps;
    (void) r;
    return FALSE;
}

/* PrintTestStyles: none available, dx 0 */
Boolean PrintTestStyles(PState *ps, DriverRegs *r)
{
    (void) ps;
    r->DR_dx = 0;
    return FALSE;
}

Boolean PrintClearStyles(PState *ps)
{
    (void) ps;
    return FALSE;
}
