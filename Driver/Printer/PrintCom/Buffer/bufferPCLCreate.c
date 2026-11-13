/*
 * bufferPCLCreate.c -- PrCreatePrintBuffers of the PCL drivers (the C
 * port of bufferPCLCreate.asm): an output buffer of twice the band's
 * bytes, enough for a scan line compressed (graphicsPCLTIFF.c).
 */
#include "printcom.h"

Boolean PrCreatePrintBuffers(PState *ps)
{
    ps->PS_bufHan = MemAlloc((word) (ps->PS_bandBWidth * 2),
			     HF_DYNAMIC | HF_SHARABLE, HAF_NO_ERR | HAF_LOCK);
    ps->PS_bufSeg = MemDeref(ps->PS_bufHan);
    return FALSE;
}
