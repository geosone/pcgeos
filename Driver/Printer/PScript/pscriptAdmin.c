/*
 * pscriptAdmin.c -- the admin routines (the C port of pscriptAdmin.asm).
 */
#include "pscript.h"

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
