/*
 * customIBMPJLToPCL.c -- the IBM 4039 (the C port of
 * customIBMPJLToPCL.asm): the universal exit and PCL 5 at the start of a
 * job.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrintEnterIBMPJL(PState *ps)
{
    return SendCodeOut(ps, pr_codes_PJLUEL) ||
	SendCodeOut(ps, pr_codes_PJLEnterPCL5);
}
