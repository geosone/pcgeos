/*
 * processNothing.c -- the nothing port (the C port of
 * processNothing.asm): no driver, the output goes nowhere.
 *
 * Port32: the asm gave the printer driver no stream at all; the C
 * printer drivers fail a write without a stream, so the spooler gives
 * them a stream of its own that takes everything.
 */
#include "spoolLocal.h"
#include <string.h>

static Boolean NothingStrategy(word function, DriverRegs *r)
{
    (void) r;
    switch (function) {
    case DR_STREAM_READ:
    case DR_STREAM_READ_BYTE:
	return TRUE;
    default:
	return FALSE;		/* writes: cx stays, all of it */
    }
}

Boolean InitNothingPort(SpoolJobInfo *j, word *portError)
{
    DriverRegs r;
    PState *ps;

    (void) portError;
    ps = MemLock(j->SJI_pstate);
    ps->PS_streamStrategy = NothingStrategy;
    MemUnlock(j->SJI_pstate);
    memset(&r, 0, sizeof(r));
    r.DR_si = PPT_NOTHING;		/* dx 0: keeps the strategy */
    CallPrinter(j, DR_PRINT_SET_STREAM, &r);
    return FALSE;
}

void ExitNothingPort(SpoolJobInfo *j)
{
    (void) j;
}
