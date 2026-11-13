/*
 * jobResetPrinterAndWait.c -- PrintResetPrinterAndWait (the C port of
 * jobResetPrinterAndWait.asm): the reset code once the stream has room
 * for it, then the printer's time to initialize itself.
 */
#include "printcom.h"
#include "printcomDriver.h"
#include <timer.h>
#include <string.h>

Boolean PrintResetPrinterAndWait(PState *ps)
{
    DriverRegs r;

    for (;;) {
	memset(&r, 0, sizeof(r));
	r.DR_ax = (word) STREAM_ROLES_WRITER;	/* STREAM_WRITE */
	r.DR_bx = ps->PS_streamToken;
	if (!ps->PS_streamStrategy ||
	    ps->PS_streamStrategy(DR_STREAM_QUERY, &r))
	    return FALSE;
	if ((sword) r.DR_ax > (sword) pr_codes_ResetPrinter[0])
	    break;
    }
    SendCodeOut(ps, pr_codes_ResetPrinter);
    TimerSleep(PR_INIT_WAIT_PERIOD);
    return FALSE;
}
