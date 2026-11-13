/*
 * bufferDestroy.c -- PrDestroyPrintBuffers (the C port of
 * bufferDestroy.asm): the bitmap's scan lines released, the buffer
 * freed.
 */
#include "printcom.h"

void PrDestroyPrintBuffers(PState *ps)
{
    ReleaseScanlines(ps);
    MemFree(ps->PS_bufHan);
    ps->PS_bufHan = 0;
    ps->PS_bufSeg = NULL;
}
