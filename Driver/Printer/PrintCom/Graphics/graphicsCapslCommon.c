/*
 * graphicsCapslCommon.c -- PrPrintABand (the C port of
 * graphicsCapslCommon.asm): a band of a CaPSL printer as a raster image
 * of its live bytes only: CSI x ` to the first byte with ink of any
 * line (x in 1/300"), then CSI count ; width ; dpi . r and the lines,
 * each from that byte to the last with ink of any line.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrPrintABand(PState *ps)
{
    const byte *band = PS_BAND_BUFFER(ps), *p;
    word bw = ps->PS_bandBWidth, h = ps->PS_bandHeight, y, n, w, lead;

    PrLoadBandBuffer(ps);
    /* the bytes from the first with ink on: the most of any line */
    for (w = 0, y = 0, p = band; y < h; y++, p += bw) {
	for (n = 0; n < bw && !p[n]; n++)
	    ;
	if (bw - n > w)
	    w = (word) (bw - n);
    }
    if (!w)
	return FALSE;
    lead = (word) (bw - w);
    if (SendCodeOut(ps, pr_codes_CSIcode) ||
	HexToAsciiStreamWrite(ps, PrAdjustForResolution(ps,
							(word) (lead << 3))) ||
	PrintStreamWriteByte(ps, '`'))
	return TRUE;
    /* up to the last byte with ink: the asm counted one at least */
    for (n = 0, y = 0, p = band + bw - 1; y < h; y++, p += bw) {
	word k;
	for (k = w; k && !p[-(sword) (w - k)]; k--)
	    ;
	if (!k)
	    k = 1;
	if (k > n)
	    n = k;
    }
    w = n;
    ps->PS_intWidth = w;
    if (SendCodeOut(ps, pr_codes_CSIcode) ||
	HexToAsciiStreamWrite(ps, (word) (w * h)) ||
	PrintStreamWriteByte(ps, ';') ||
	HexToAsciiStreamWrite(ps, w) ||
	SendCodeOut(ps, pr_codes_SetGraphics + ps->PS_mode * 4))
	return TRUE;
    for (y = 0, p = band + lead; y < h; y++, p += bw)
	if (PrintStreamWrite(ps, p, w))
	    return TRUE;
    return FALSE;
}
