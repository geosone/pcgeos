/*
 * bufferCreate.c -- PrCreatePrintBuffers (the C port of bufferCreate.asm):
 * the output buffer and a band buffer of PS_buffHeight scan lines.
 */
#include "printcom.h"

Boolean PrCreatePrintBuffers(PState *ps)
{
    ps->PS_bufHan = MemAlloc((word) (ps->PS_bandBWidth * ps->PS_buffHeight +
				     PRINT_OUTPUT_BUFFER_SIZE),
			     HF_DYNAMIC | HF_SHARABLE, HAF_LOCK | HAF_NO_ERR);
    ps->PS_bufSeg = MemDeref(ps->PS_bufHan);
    return FALSE;
}
