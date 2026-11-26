/*
 * streamWrite.c -- after streamWrite.asm: PrintStreamWrite.
 * Included by the printcom*.c files whose asm includes it.
 */
#include "printcom.h"
#include <string.h>

static Boolean StreamCall(PState *ps, word function, DriverRegs *r)
{
    r->DR_ax = STREAM_BLOCK;
    r->DR_bx = ps->PS_streamToken;
    if (!ps->PS_streamStrategy || ps->PS_streamStrategy(function, r)) {
	ps->PS_error = 1;
	return TRUE;
    }
    return FALSE;
}

/* PrintStreamWrite: ds:si = buffer, cx = count */
Boolean PrintStreamWrite(PState *ps, const void *buf, word size)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_cx = size;
    r.DR_dsSi = (void *) buf;
    return StreamCall(ps, DR_STREAM_WRITE, &r);
}
