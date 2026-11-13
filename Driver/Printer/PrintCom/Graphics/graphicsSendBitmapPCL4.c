/*
 * graphicsSendBitmapPCL4.c -- a swath as one raster graphic (the C port
 * of graphicsSendBitmapPCL4.asm): each scan line without its trailing
 * white bytes, at least one byte.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrSendBitmap(PState *ps)
{
    static const byte white = 0;
    word lines = ps->PS_swath.B_height, n;
    const byte *line;

    if (SendCodeOut(ps, pr_codes_StartGraphics))
	return TRUE;
    DerefFirstScanline(ps);
    while (lines--) {
	line = DerefAScanline(ps);
	n = line ? ps->PS_bandBWidth : 0;
	while (n > 1 && !line[n - 1])
	    n--;
	if (!n) {
	    line = &white;
	    n = 1;
	}
	if (WriteNumCommand(ps, pr_codes_TransferGraphics, n) ||
	    PrintStreamWrite(ps, line, n))
	    return TRUE;
	ps->PS_newScanNumber++;
    }
    return SendCodeOut(ps, pr_codes_EndGraphics);
}
