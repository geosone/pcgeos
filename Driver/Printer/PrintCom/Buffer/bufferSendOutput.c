/*
 * bufferSendOutput.c -- PrSendOutputBuffer (the C port of
 * bufferSendOutput.asm): the bytes in the output buffer to the printer;
 * *count (di in the asm) is 0 after.
 */
#include "printcom.h"

Boolean PrSendOutputBuffer(PState *ps, word *count)
{
    word n = *count;

    *count = 0;
    return n ? PrintStreamWrite(ps, PS_OUTPUT_BUFFER(ps), n) : FALSE;
}
