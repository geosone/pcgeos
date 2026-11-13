/*
 * rotate3x4.c -- PrRotate4LinesZeroTop and PrRotate4LinesZeroBottom (the
 * C port of rotate3x4.asm): 4 scan lines into print head columns on
 * every other pin, starting with the top pin (zero bottom: passes 1 and
 * 3 of the 120 x 108 dpi mode) or the one below it (zero top: pass 2).
 */
#include "printcom.h"

static Boolean Rotate4(PState *ps, word bytes, int shift)
{
    const byte *col = PS_BAND_BUFFER(ps);
    byte *out = PS_OUTPUT_BUFFER(ps);
    word n = 0, x, line;
    int bit;
    byte c;

    for (x = 0; x < bytes; x++, col++) {
	for (bit = 7; bit >= 0; bit--) {
	    c = 0;
	    for (line = 0; line < 4; line++)
		c = (byte) ((c << 2) |
			    (((col[line * ps->PS_bandBWidth] >> bit) & 1)
			     << shift));
	    out[n++] = c;
	}
	if (n >= PRINT_OUTPUT_BUFFER_SIZE && PrSendOutputBuffer(ps, &n))
	    return TRUE;
    }
    return PrSendOutputBuffer(ps, &n);
}

Boolean PrRotate4LinesZeroTop(PState *ps, word bytes)
{
    return Rotate4(ps, bytes, 0);
}

Boolean PrRotate4LinesZeroBottom(PState *ps, word bytes)
{
    return Rotate4(ps, bytes, 1);
}
