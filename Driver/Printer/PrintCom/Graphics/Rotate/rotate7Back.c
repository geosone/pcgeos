/*
 * rotate7Back.c -- PrRotate7Lines (the C port of rotate7Back.asm): 7 scan
 * lines of the band buffer into print head columns, a byte a column with
 * the top line in bit 0 (bit 7 clear), through the output buffer to the
 * printer.
 */
#include "printcom.h"

Boolean PrRotate7Lines(PState *ps, word bytes)
{
    const byte *col = PS_BAND_BUFFER(ps);
    byte *out = PS_OUTPUT_BUFFER(ps);
    word n = 0, x;
    int bit, line;
    byte c;

    for (x = 0; x < bytes; x++, col++) {
	for (bit = 7; bit >= 0; bit--) {
	    c = 0;
	    for (line = 6; line >= 0; line--)
		c = (byte) ((c << 1) |
			    ((col[line * ps->PS_bandBWidth] >> bit) & 1));
	    out[n++] = c;
	}
	if (n >= PRINT_OUTPUT_BUFFER_SIZE && PrSendOutputBuffer(ps, &n))
	    return TRUE;
    }
    return PrSendOutputBuffer(ps, &n);
}
