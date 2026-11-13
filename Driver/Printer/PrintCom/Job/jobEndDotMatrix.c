/*
 * jobEndDotMatrix.c -- PrintEndJob (the C port of jobEndDotMatrix.asm):
 * the device's custom exit routine if it has one, else the styles off and
 * the printer reset.
 */
#include "printcom.h"

Boolean PrintEndJob(PState *ps, DriverRegs *r)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;

    (void) r;
    if (pi->PI_customExit)
	return printCustomRoutines[pi->PI_customExit](ps);
    PrintClearStyles(ps);
    return PrintResetPrinterAndWait(ps);
}
