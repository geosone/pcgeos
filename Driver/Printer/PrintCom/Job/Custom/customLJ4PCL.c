/*
 * customLJ4PCL.c -- the LaserJet 4 (the C port of customLJ4PCL.asm):
 * PJL with a message on the display at the start of a job, the message
 * cleared at its end.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrintEnterLJ4PCL(PState *ps)
{
    return SendCodeOut(ps, pr_codes_PJLUEL) ||
	SendCodeOut(ps, pr_codes_PJLCRLF) ||
	SendCodeOut(ps, pr_codes_PJLMsgPCL) ||
	SendCodeOut(ps, pr_codes_PJLEnterPCL);
}

Boolean PrintExitLJ4PCL(PState *ps)
{
    return SendCodeOut(ps, pr_codes_PJLUEL) ||
	SendCodeOut(ps, pr_codes_PJLCRLF) ||
	SendCodeOut(ps, pr_codes_PJLMsgClear) ||
	SendCodeOut(ps, pr_codes_PJLUEL);
}
