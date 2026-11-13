/*
 * graphicsPrintSwathPCLKCMY.c -- PrintSwath (the C port of
 * graphicsPrintSwathPCLKCMY.asm): a swath of the spooler to a color PCL
 * printer, a scan line at a time: its 4 planes (black, cyan, magenta, yellow: the
 * planes of the band at 3, 1, 2, 0 times its byte width), each a raster plane,
 * then the row ended by Pr1ScanlineFeed.
 */
#include "printcom.h"
#include "printcomDriver.h"

static const word colorOffset[4] = { 3, 1, 2, 0 };

/* PrintSwath: dx = the VM file, cx = the bitmap's block */
Boolean PrintSwath(PState *ps, DriverRegs *r)
{
    word lines, left, c;
    const byte *line;
    Boolean err = FALSE;

    LoadSwathHeader(ps, (VMFileHandle) r->DR_dx, (VMBlockHandle) r->DR_cx);
    lines = PrLoadPstateVars(ps, &left);
    PrCreatePrintBuffers(ps);
    ps->PS_curColorNumber = 0;
    DerefFirstScanline(ps);
    if (lines) {
	err = WriteNumCommand(ps, pr_codes_SetGraphicRes,
			      pr_graphic_Res_Values[ps->PS_mode / 2]);
	while (!err && lines--) {
	    ps->PS_curColorNumber = 0;		/* the scan line's start */
	    line = DerefAScanline(ps);
	    for (c = 0; !err && c < 4; c++) {
		ps->PS_curColorNumber = c;
		err = PrSendScanline(ps, line ? line + colorOffset[c] *
				     ps->PS_bandBWidth : NULL);
	    }
	    ps->PS_newScanNumber++;
	    if (!err)
		err = Pr1ScanlineFeed(ps);
	}
	if (!err)
	    err = SendCodeOut(ps, pr_codes_EndGraphics);
    }
    PrDestroyPrintBuffers(ps);
    return err;
}
