/*
 * rotate48.c -- PrRotate48Lines (the C port of rotate48.asm): the band
 * buffer's HI_RES_BUFF_HEIGHT (48) scan lines into print head columns of
 * six bytes (48 pins, the top one in bit 7 of the first byte), through
 * the output buffer to the printer.
 */
#include "printcom.h"
#include "printcomDriver.h"

#define PINS	48

Boolean PrRotate48Lines(PState *ps, word bytes)
{
    const byte *col = PS_BAND_BUFFER(ps);
    byte *out = PS_OUTPUT_BUFFER(ps);
    word n = 0, x, line, lines = HI_RES_BUFF_HEIGHT;
    int bit, k;
    byte v;

    if (lines > PINS)
	lines = PINS;
    for (x = 0; x < bytes; x++, col++) {
	for (bit = 7; bit >= 0; bit--)
	    for (k = 0; k < PINS / 8; k++) {
		v = 0;
		for (line = (word) (k * 8); line < (word) (k * 8 + 8); line++)
		    v = (byte) ((v << 1) |
				(line < lines
				 ? (col[line * ps->PS_bandBWidth] >> bit) & 1
				 : 0));
		out[n++] = v;
	    }
	if (n >= PRINT_OUTPUT_BUFFER_SIZE && PrSendOutputBuffer(ps, &n))
	    return TRUE;
    }
    return PrSendOutputBuffer(ps, &n);
}
