/*
 * graphicsSendBitmapCompressedPCL4.c -- a swath as the runs of its scan
 * lines (the C port of graphicsSendBitmapCompressedPCL4.asm): the runs of
 * a line are split where ENOUGH_BLANK white bytes or more are between
 * them, each run a raster graphic of its own at its place (ESC * p # Y,
 * ESC * p # X in 1/300"); white lines are not sent.  The printer then
 * needs memory for the runs only.
 */
#include "printcom.h"
#include "printcomDriver.h"

/* PrSendGraphicData: bytes [start, end) of the line */
static Boolean SendRun(PState *ps, const byte *line, word start, word end)
{
    if (start == end)
	return FALSE;
    return WriteNumCommand(ps, pr_codes_AdjustInY,
			   (word) ps->PS_cursorPos.P_y) ||
	WriteNumCommand(ps, pr_codes_AdjustInX,
			(word) (PrAdjustForResolution(ps,
						      (word) (start * 8)) +
				ps->PS_cursorPos.P_x)) ||
	WriteNumCommand(ps, pr_codes_StartAndTransferGraphics,
			(word) (end - start)) ||
	PrintStreamWrite(ps, line + start, (word) (end - start)) ||
	SendCodeOut(ps, pr_codes_EndGraphics);
}

Boolean PrSendBitmapCompressed(PState *ps)
{
    word lines = ps->PS_swath.B_height, w = ps->PS_bandBWidth;
    word i, data, zeros;
    const byte *line;

    DerefFirstScanline(ps);
    while (lines--) {
	line = DerefAScanline(ps);
	if (line) {
	    data = 0;
	    i = 0;
	    for (;;) {
		while (i < w && line[i])
		    i++;
		if (i == w) {			/* data to the end */
		    if (SendRun(ps, line, data, w))
			return TRUE;
		    break;
		}
		zeros = i;
		while (i < w && !line[i])
		    i++;
		if (i == w) {			/* white to the end */
		    if (SendRun(ps, line, data, zeros))
			return TRUE;
		    break;
		}
		if (i - zeros >= ENOUGH_BLANK) {
		    if (SendRun(ps, line, data, zeros))
			return TRUE;
		    data = i;
		}
	    }
	}
	ps->PS_cursorPos.P_y = (sword) (ps->PS_cursorPos.P_y +
					PrAdjustForResolution(ps, 1));
	ps->PS_newScanNumber++;
    }
    return FALSE;
}
