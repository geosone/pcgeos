/*
 * customTotalResetPCL.c -- printers that keep the job's settings (the C
 * port of customTotalResetPCL.asm): at the end of a job a reset, the init
 * codes, one copy and the letter length.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrintExitTotalResetPCL(PState *ps)
{
    return SendCodeOut(ps, pr_codes_ResetPrinter) ||
	SendCodeOut(ps, pr_codes_InitPrinter) ||
	WriteNumCommand(ps, pr_codes_NumCopies, 1) ||
	SendCodeOut(ps, pr_codes_SetLetterLength);
}
