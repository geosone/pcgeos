/*
 * printcomAdmin.c -- the admin routines of a driver with nothing to do
 * in them (the C port of printcomAdmin.asm).
 */
#include "printcom.h"

Boolean PrintInit(DriverRegs *r)
{
    (void) r;
    return FALSE;
}

Boolean PrintExit(DriverRegs *r)
{
    (void) r;
    return FALSE;
}

Boolean PrintInitStream(PState *ps)
{
    (void) ps;
    return FALSE;
}
