/*
 * graphicsPrintSwathPCL.c -- PrintSwath of the PCL drivers (the C port
 * of graphicsPrintSwathPCL.asm): the raster resolution and start, the
 * swath scan line by scan line (bands of one line), the raster end.
 */
#include "printcom.h"
#include "printcomDriver.h"

/* PrintSwath: dx = the VM file, cx = the bitmap's block */
Boolean PrintSwath(PState *ps, DriverRegs *r)
{
    word lines, left;
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
	    err = PrSendScanline(ps, DerefAScanline(ps));
	    ps->PS_newScanNumber++;
	}
	if (!err)
	    err = SendCodeOut(ps, pr_codes_EndGraphics);
    }
    PrDestroyPrintBuffers(ps);
    return err;
}
