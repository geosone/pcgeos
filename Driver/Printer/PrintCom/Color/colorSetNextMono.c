/*
 * colorSetNextMono.c -- SetNextCMYK of the monochrome printers (the C
 * port of colorSetNextMono.asm): nothing.
 */
#include "printcom.h"

Boolean SetNextCMYK(PState *ps)
{
    (void) ps;
    return FALSE;
}
