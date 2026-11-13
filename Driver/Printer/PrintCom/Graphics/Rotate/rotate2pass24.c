/*
 * rotate2pass24.c -- PrSend24HiresLines (the C port of
 * rotate2pass24.asm): 24 scan lines printed twice at the high
 * resolution, the even columns, then the odd ones, so that no pass
 * prints two dots side by side (the rotation is PrRotate24Columns,
 * rotate24.c).
 */
#include "printcom.h"
#include "printcomDriver.h"

/* each color plane from *bandStart */
Boolean PrSend24HiresLines(PState *ps, word *bandStart)
{
    word cols, bytes;

    for (;;) {
	PrLoadBandBuffer(ps);
	cols = PrScanBandBuffer(ps, &bytes);
	if (cols &&
	    (PrSendGraphicControlCode(ps, pr_codes_SetHiGraphics, cols) ||
	     PrRotate24Columns(ps, bytes, 0) ||
	     PrSendGraphicControlCode(ps, pr_codes_SetHiGraphics, cols) ||
	     PrRotate24Columns(ps, bytes, 1)))
	    return TRUE;
	if (SetNextCMYK(ps))
	    return TRUE;
	if (!ps->PS_curColorNumber)
	    return FALSE;
	ps->PS_newScanNumber = *bandStart;
    }
}
