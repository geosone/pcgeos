/*
 * rotate2pass8.c -- PrSend8HiresLines and its rotations (the C port of
 * rotate2pass8.asm): 8 scan lines printed twice at the high resolution,
 * the even columns, then the odd ones, so that no pass prints two dots
 * side by side; a column left out is 0.
 */
#include "printcom.h"
#include "printcomDriver.h"

/* odd: 0 for the even columns (0, 2, ...), 1 for the odd ones */
static Boolean Rotate8Columns(PState *ps, word bytes, int odd)
{
    const byte *col = PS_BAND_BUFFER(ps);
    byte *out = PS_OUTPUT_BUFFER(ps);
    word n = 0, x, line;
    int bit;
    byte c;

    for (x = 0; x < bytes; x++, col++) {
	for (bit = 7; bit >= 0; bit--) {
	    c = 0;
	    if (((7 - bit) & 1) == odd)
		for (line = 0; line < 8; line++)
		    c = (byte) ((c << 1) |
				((col[line * ps->PS_bandBWidth] >> bit) & 1));
	    out[n++] = c;
	}
	if (n >= PRINT_OUTPUT_BUFFER_SIZE && PrSendOutputBuffer(ps, &n))
	    return TRUE;
    }
    return PrSendOutputBuffer(ps, &n);
}

/* each color plane from *bandStart */
Boolean PrSend8HiresLines(PState *ps, word *bandStart)
{
    word cols, bytes;

    for (;;) {
	PrLoadBandBuffer(ps);
	cols = PrScanBandBuffer(ps, &bytes);
	if (cols &&
	    (PrSendGraphicControlCode(ps, pr_codes_SetHiGraphics, cols) ||
	     Rotate8Columns(ps, bytes, 0) ||
	     PrSendGraphicControlCode(ps, pr_codes_SetHiGraphics, cols) ||
	     Rotate8Columns(ps, bytes, 1)))
	    return TRUE;
	if (SetNextCMYK(ps))
	    return TRUE;
	if (!ps->PS_curColorNumber)
	    return FALSE;
	ps->PS_newScanNumber = *bandStart;
    }
}
