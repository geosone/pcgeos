/*
 * graphicsPCLTIFF.c -- a scan line in PCL raster graphics (the C port of
 * graphicsPCLTIFF.asm): without its trailing white bytes, compressed
 * as TIFF PackBits (ESC *b2m) when PUID_compressionMode has bit 1, else
 * as it is (ESC *b0m); a white line is a line of one zero byte.
 *
 * Port32: the PackBits runs are made by a plain encoder (a run of two or
 * more equal bytes is repeated, the rest literal, 128 bytes a packet at
 * most); the asm's encoder decodes to the same bytes, its packets may
 * differ.
 */
#include "printcom.h"
#include "printcomDriver.h"

/* PackBits of n > 0 bytes to out: the length */
static word PackBits(const byte *in, word n, byte *out)
{
    word i = 0, o = 0, j, run;

    while (i < n) {
	for (run = 1; i + run < n && run < 128 && in[i + run] == in[i]; run++)
	    ;
	if (run >= 2) {
	    out[o++] = (byte) (257 - run);
	    out[o++] = in[i];
	    i = (word) (i + run);
	    continue;
	}
	/* a literal up to the next run of two */
	for (j = i + 1; j < n && j - i < 128 &&
		 !(j + 1 < n && in[j] == in[j + 1]); j++)
	    ;
	out[o++] = (byte) (j - i - 1);
	while (i < j)
	    out[o++] = in[i++];
    }
    return o;
}

/* PrSendScanline: the scan line (NULL: white) of PS_bandBWidth bytes */
Boolean PrSendScanline(PState *ps, const byte *line)
{
    word n = line ? ps->PS_bandBWidth : 0;
    byte *out = (byte *) ps->PS_bufSeg;

    while (n && !line[n - 1])
	n--;
    if (!n)
	return SendCodeOut(ps, pr_codes_TransferNoGraphics);
    if (PS_UI_DATA(ps)->PUID_compressionMode & 2) {
	n = PackBits(line, n, out);
	return WriteNumCommand(ps, pr_codes_TIFFTransferGraphics, n) ||
	    PrintStreamWrite(ps, out, n);
    }
    return WriteNumCommand(ps, pr_codes_TransferGraphics, n) ||
	PrintStreamWrite(ps, line, n);
}
