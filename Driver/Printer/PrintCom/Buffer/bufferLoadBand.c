/*
 * bufferLoadBand.c -- PrLoadBandBuffer (the C port of
 * bufferLoadBand.asm): PS_buffHeight scan lines into the band buffer,
 * from PS_newScanNumber on, PS_interleaveFactor apart.  Scan lines past
 * the bitmap are zeros (see PS_bitmapFile).
 */
#include "printcom.h"
#include <string.h>

void PrLoadBandBuffer(PState *ps)
{
    byte *d = PS_BAND_BUFFER(ps);
    const byte *s;
    word i;

    for (i = 0; i < ps->PS_buffHeight; i++) {
	s = DerefAScanline(ps);
	if (s)
	    memcpy(d, s, ps->PS_bandBWidth);
	else
	    memset(d, 0, ps->PS_bandBWidth);
	d += ps->PS_bandBWidth;
	ps->PS_newScanNumber = (word) (ps->PS_curScanNumber +
				       ps->PS_interleaveFactor);
    }
}
