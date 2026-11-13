/*
 * customPJLToPCL.c -- PJL printers (the C port of customPJLToPCL.asm):
 * the universal exit and PCL at the start of a job; at its end a reset
 * and the universal exit.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrintEnterPJL(PState *ps)
{
    return SendCodeOut(ps, pr_codes_PJLUEL) ||
	SendCodeOut(ps, pr_codes_PJLCRLF) ||
	SendCodeOut(ps, pr_codes_PJLEnterPCL);
}

Boolean PrintExitPJL(PState *ps)
{
    return SendCodeOut(ps, pr_codes_ResetPrinter) ||
	SendCodeOut(ps, pr_codes_PJLUEL);
}
