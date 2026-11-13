/*
 * jobEndDummy.c -- PrintEndJob (the C port of jobEndDummy.asm): the
 * device's custom exit, or nothing.
 */
#include "printcom.h"

Boolean PrintEndJob(PState *ps, DriverRegs *r)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;

    (void) r;
    if (pi->PI_customExit)
	return printCustomRoutines[pi->PI_customExit](ps);
    return FALSE;
}
