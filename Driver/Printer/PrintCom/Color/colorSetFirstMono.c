/*
 * colorSetFirstMono.c -- SetFirstCMYK of the monochrome printers (the C
 * port of colorSetFirstMono.asm): nothing.
 */
#include "printcom.h"

Boolean SetFirstCMYK(PState *ps)
{
    (void) ps;
    return FALSE;
}
