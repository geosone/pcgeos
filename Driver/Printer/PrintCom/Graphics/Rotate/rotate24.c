/*
 * rotate24.c -- PrRotate24Lines (the C port of rotate24.asm): the band
 * buffer's MED_RES_BUFF_HEIGHT scan lines into print head columns of
 * three bytes (24 pins, the top one in bit 7 of the first byte), through
 * the output buffer to the printer.  Fewer lines than 24 go to the
 * bottom pins, as in the asm.  PrRotate24Columns serves rotate2pass24.c.
 */
#include "printcom.h"
#include "printcomDriver.h"

#ifndef MED_RES_BUFF_HEIGHT
#define MED_RES_BUFF_HEIGHT	24
#endif

/* columns: -1 all of them, 0 the even ones, 1 the odd ones (the others
 * 0) */
Boolean PrRotate24Columns(PState *ps, word bytes, int columns)
{
    const byte *col = PS_BAND_BUFFER(ps);
    byte *out = PS_OUTPUT_BUFFER(ps);
    word n = 0, x, line;
    int bit;
    unsigned long c;

    for (x = 0; x < bytes; x++, col++) {
	for (bit = 7; bit >= 0; bit--) {
	    c = 0;
	    if (columns < 0 || ((7 - bit) & 1) == columns)
		for (line = 0; line < MED_RES_BUFF_HEIGHT; line++)
		    c = (c << 1) |
			((col[line * ps->PS_bandBWidth] >> bit) & 1);
	    out[n++] = (byte) (c >> 16);
	    out[n++] = (byte) (c >> 8);
	    out[n++] = (byte) c;
	}
	if (n >= PRINT_OUTPUT_BUFFER_SIZE && PrSendOutputBuffer(ps, &n))
	    return TRUE;
    }
    return PrSendOutputBuffer(ps, &n);
}

Boolean PrRotate24Lines(PState *ps, word bytes)
{
    return PrRotate24Columns(ps, bytes, -1);
}
