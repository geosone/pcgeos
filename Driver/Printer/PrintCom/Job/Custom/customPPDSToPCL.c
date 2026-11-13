/*
 * customPPDSToPCL.c -- the IBM 4019/4029 in PPDS mode (the C port of
 * customPPDSToPCL.asm): to PCL at the start of a job, back to PPDS at its
 * end (after a reset, instead of the standard reset).
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrintGoToPCL(PState *ps)
{
    return SendCodeOut(ps, pr_codes_SICToPCL);
}

Boolean PrintGoToPPDS(PState *ps)
{
    return SendCodeOut(ps, pr_codes_ResetPrinter) ||
	SendCodeOut(ps, pr_codes_SICToPPDS);
}
