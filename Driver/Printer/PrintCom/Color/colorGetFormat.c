/*
 * colorGetFormat.c -- PrintGetColorFormat (the C port of
 * colorGetFormat.asm): al = the BMFormat of the device (PT_COLOR).
 */
#include "printcom.h"

Boolean PrintGetColorFormat(PState *ps, DriverRegs *r)
{
    r->DR_ax = ps->PS_deviceInfo->PI_type & PT_COLOR;
    return FALSE;
}
