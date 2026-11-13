/*
 * jobEndCanonBJC.c -- PrintEndJob (the C port of jobEndCanonBJC.asm):
 * the device's custom exit, or back to the emulation mode.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrintEndJob(PState *ps, DriverRegs *r)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;

    (void) r;
    if (pi->PI_customExit)
	return printCustomRoutines[pi->PI_customExit](ps);
    return SendCodeOut(ps, pr_codes_ReturnToEmulationMode);
}
