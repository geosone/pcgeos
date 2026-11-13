/*
 * jobEndDotMatrix.c -- PrintEndJob (the C port of jobEndDotMatrix.asm):
 * the styles off, the printer reset.
 */
#include "printcom.h"

Boolean PrintEndJob(PState *ps, DriverRegs *r)
{
    (void) r;
    PrintClearStyles(ps);
    return PrintResetPrinterAndWait(ps);
}
