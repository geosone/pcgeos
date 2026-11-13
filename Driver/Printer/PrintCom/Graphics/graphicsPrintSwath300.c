/*
 * graphicsPrintSwath300.c -- PrintSwath (the C port of
 * graphicsPrintSwath300.asm): a swath to a page printer in 1/300", a
 * band at a time, the cursor moved down absolutely after each (a band is
 * 32 dots in every resolution), after the last part band by its lines.
 */
#include "printcom.h"
#include "printcomDriver.h"

/* PrintSwath: dx = the VM file, cx = the bitmap's block */
Boolean PrintSwath(PState *ps, DriverRegs *r)
{
    word bands, left;
    Boolean err = FALSE;

    LoadSwathHeader(ps, (VMFileHandle) r->DR_dx, (VMBlockHandle) r->DR_cx);
    bands = PrLoadPstateVars(ps, &left);
    PrCreatePrintBuffers(ps);
    ps->PS_curColorNumber = 0;
    DerefFirstScanline(ps);
    SetFirstCMYK(ps);
    while (!err && bands--) {
	err = PrPrintABand(ps);
	if (!err) {
	    ps->PS_cursorPos.P_y = (sword) (ps->PS_cursorPos.P_y +
					    HI_RES_BAND_HEIGHT);
	    err = PrMoveCursor(ps);
	}
    }
    if (!err && left) {
	err = PrPrintABand(ps);
	if (!err) {
	    ps->PS_cursorPos.P_y = (sword) (ps->PS_cursorPos.P_y +
					    PrAdjustForResolution(ps, left));
	    err = PrMoveCursor(ps);
	}
    }
    PrDestroyPrintBuffers(ps);
    return err;
}
