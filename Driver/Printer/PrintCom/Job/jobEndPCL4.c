/*
 * jobEndPCL4.c -- PrintEndJob of the PCL drivers (the C port of
 * jobEndPCL4.asm): the printer reset, or the device's custom exit routine
 * instead.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrintEndJob(PState *ps, DriverRegs *r)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;

    (void) r;
    if (pi->PI_customExit)
	return printCustomRoutines[pi->PI_customExit](ps);
    return SendCodeOut(ps, pr_codes_ResetPrinter);
}
