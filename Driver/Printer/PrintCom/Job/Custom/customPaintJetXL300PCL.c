/*
 * customPaintJetXL300PCL.c -- the custom entry and exit of the PaintJet
 * XL300 (the C port of customPaintJetXL300PCL.asm): into PCL by PJL with
 * the reset and no negative motion, the start going on past its own reset
 * (InitPState); out with the reset and the PJL UEL.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrintEnterPJL(PState *ps)
{
    if (SendCodeOut(ps, pr_codes_PJLEntryAndResetNoNeg))
	return TRUE;
    return PR_CUSTOM_AT_INIT_PSTATE;
}

Boolean PrintExitPJL(PState *ps)
{
    return SendCodeOut(ps, pr_codes_ResetPrinter) ||
	SendCodeOut(ps, pr_codes_PJLExit);
}
