/*
 * graphicsPCLScanline.c -- PrSendScanline (the C port of
 * graphicsPCLScanline.asm): a raster line up to its last byte not 0,
 * uncompressed; a white one as the line feed code (a raster line of a
 * zero byte).
 */
#include "printcom.h"
#include "printcomDriver.h"

/* PrSendScanline: the scan line (NULL: white) of PS_bandBWidth bytes */
Boolean PrSendScanline(PState *ps, const byte *line)
{
    word n = line ? ps->PS_bandBWidth : 0;

    while (n && !line[n - 1])
	n--;
    if (!n)
	return SendCodeOut(ps, pr_codes_Do1ScanlineFeed);
    return WriteNumCommand(ps, pr_codes_TransferGraphics, n) ||
	PrintStreamWrite(ps, line, n);
}
