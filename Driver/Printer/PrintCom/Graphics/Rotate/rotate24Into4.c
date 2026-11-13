/*
 * rotate24Into4.c -- PrRotate24Lines (the C port of rotate24Into4.asm)
 * and the column rotation of rotate2pass24Into4.c: the band buffer's
 * HI_RES_BUFF_HEIGHT scan lines into print head columns of four bytes of
 * six pins each (Toshiba), through the output buffer to the printer.
 *
 * As in the asm, a column's lines are shifted into one byte from the
 * top on, and the byte is stored when 18, 12, 6 and 0 lines are left;
 * it is not cleared between, so the two bits above the six keep the
 * lines before (fewer lines than 24 go to the bottom pins).
 */
#include "printcom.h"
#include "printcomDriver.h"

/* columns: -1 all of them, 0 the even ones, 1 the odd ones (the others
 * 0) */
Boolean PrRotate24Into4Columns(PState *ps, word bytes, int columns)
{
    const byte *col = PS_BAND_BUFFER(ps), *p;
    byte *out = PS_OUTPUT_BUFFER(ps);
    byte acc[8];
    word n = 0, x;
    int c, left, g;

    if (columns >= 0)
	PrClearOutputBuffer(ps);
    for (x = 0; x < bytes; x++, col++) {
	for (c = 0; c < 8; c++)
	    acc[c] = 0;
	p = col;
	g = 0;
	for (left = HI_RES_BUFF_HEIGHT; left > 0; ) {
	    for (c = 0; c < 8; c++)	/* column c: bit 7 - c */
		acc[c] = (byte) ((acc[c] << 1) | ((*p >> (7 - c)) & 1));
	    p += ps->PS_bandBWidth;
	    left--;
	    if (left == 18 || left == 12 || left == 6 || left == 0) {
		for (c = 0; c < 8; c++)
		    if (columns < 0 || (c & 1) == columns)
			out[n + 4 * c + g] = acc[c];
		g++;
	    }
	}
	n = (word) (n + 32);
	if (n >= PRINT_OUTPUT_BUFFER_SIZE && PrSendOutputBuffer(ps, &n))
	    return TRUE;
    }
    return PrSendOutputBuffer(ps, &n);
}

Boolean PrRotate24Lines(PState *ps, word bytes)
{
    return PrRotate24Into4Columns(ps, bytes, -1);
}
