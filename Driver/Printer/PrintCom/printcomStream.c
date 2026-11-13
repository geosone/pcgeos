/*
 * printcomStream.c -- writing to the stream (the C port of
 * printcomStream.asm: Stream/streamWrite.asm, streamWriteByte.asm and
 * streamSendCodeOut.asm).
 *
 * DR_STREAM_WRITE and DR_STREAM_WRITE_BYTE with STREAM_BLOCK to the
 * PState's stream; an error sets PS_error to 1 and returns the carry.
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

/* PrintStreamWriteByte: cl = the byte */
Boolean PrintStreamWriteByte(PState *ps, byte b)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_cx = b;
    return StreamCall(ps, DR_STREAM_WRITE_BYTE, &r);
}

/* SendCodeOut: a count byte, then that many bytes; none is no write */
Boolean SendCodeOut(PState *ps, const byte *code)
{
    if (!code[0])
	return FALSE;
    return PrintStreamWrite(ps, code + 1, code[0]);
}
