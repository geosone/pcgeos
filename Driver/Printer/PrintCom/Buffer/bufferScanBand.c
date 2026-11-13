/*
 * bufferScanBand.c -- PrScanBandBuffer (the C port of
 * bufferScanBand.asm): how wide the band buffer has something to print:
 * the bytes up to the last that is not 0 in any of its lines; the
 * columns (8 a byte) are returned.
 */
#include "printcom.h"

word PrScanBandBuffer(PState *ps, word *bytes)
{
    const byte *line = PS_BAND_BUFFER(ps);
    word w = 0, n, i;

    for (i = 0; i < ps->PS_buffHeight && w < ps->PS_bandBWidth; i++) {
	for (n = ps->PS_bandBWidth; n > w && !line[n - 1]; n--)
	    ;
	if (n > w)
	    w = n;
	line += ps->PS_bandBWidth;
    }
    *bytes = w;
    return (word) (w << 3);
}
