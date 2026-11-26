/*
 * streamWriteByte.c -- after streamWriteByte.asm: PrintStreamWriteByte.
 * Included by the printcom*.c files whose asm includes it.
 */
#include "printcom.h"
#include <string.h>

/* PrintStreamWriteByte: cl = the byte */
Boolean PrintStreamWriteByte(PState *ps, byte b)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_cx = b;
    return StreamCall(ps, DR_STREAM_WRITE_BYTE, &r);
}
