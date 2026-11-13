/*
 * rotate8.c -- PrRotate8Lines (the C port of rotate8.asm): 8 scan lines
 * of the band buffer into print head columns, a byte a column with the
 * top line in bit 7, through the output buffer to the printer.
 */
#include "printcom.h"

Boolean PrRotate8Lines(PState *ps, word bytes)
{
    const byte *col = PS_BAND_BUFFER(ps);
    byte *out = PS_OUTPUT_BUFFER(ps);
    word n = 0, x, line;
    int bit;
    byte c;

    for (x = 0; x < bytes; x++, col++) {
	for (bit = 7; bit >= 0; bit--) {
	    c = 0;
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
