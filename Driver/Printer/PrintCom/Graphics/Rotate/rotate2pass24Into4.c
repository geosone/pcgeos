/*
 * rotate2pass24Into4.c -- PrSend24HiresLines (the C port of
 * rotate2pass24Into4.asm): 24 scan lines printed twice at the high
 * resolution, the even columns, then the odd ones, in columns of four
 * six-pin bytes (Toshiba; the rotation is in rotate24Into4.c).
 */
#include "printcom.h"
#include "printcomDriver.h"

extern Boolean PrRotate24Into4Columns(PState *ps, word bytes, int columns);

/* each color plane from *bandStart */
Boolean PrSend24HiresLines(PState *ps, word *bandStart)
{
    word cols, bytes;

    for (;;) {
	PrLoadBandBuffer(ps);
	cols = PrScanBandBuffer(ps, &bytes);
	if (cols &&
	    (PrSendGraphicControlCode(ps, pr_codes_SetHiGraphics, cols) ||
	     PrRotate24Into4Columns(ps, bytes, 0) ||
	     PrSendGraphicControlCode(ps, pr_codes_SetHiGraphics, cols) ||
	     PrRotate24Into4Columns(ps, bytes, 1)))
	    return TRUE;
	if (SetNextCMYK(ps))
	    return TRUE;
	if (!ps->PS_curColorNumber)
	    return FALSE;
	ps->PS_newScanNumber = *bandStart;
    }
}
