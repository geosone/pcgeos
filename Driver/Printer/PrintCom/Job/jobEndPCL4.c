/*
 * jobEndPCL4.c -- PrintEndJob of the PCL drivers (the C port of
 * jobEndPCL4.asm): the printer reset.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrintEndJob(PState *ps, DriverRegs *r)
{
    (void) r;
    /* PI_customExit: none in these drivers (dmdevinfo2c.py) */
    return SendCodeOut(ps, pr_codes_ResetPrinter);
}
