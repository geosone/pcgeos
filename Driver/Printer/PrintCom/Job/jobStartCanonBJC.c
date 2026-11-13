/*
 * jobStartCanonBJC.c -- PrintStartJob (the C port of
 * jobStartCanonBJC.asm): the printer reset (and its time), the paper
 * path, the cursor home, the raster mode (PackBits, CMYK of a bit), the
 * resolution of the mode (vertical, then horizontal, high byte first)
 * and the print method with the quality, draft in the low resolution.
 */
#include "printcom.h"
#include "printcomDriver.h"

/* PrintStartJob: dx:si (DR_dsSi) = the JobParameters (also after the
 * PState) */
Boolean PrintStartJob(PState *ps, DriverRegs *r)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;
    Boolean low;
    word x, y;

    (void) r;
    ps->PS_printerType = pi->PI_type;
    ps->PS_printerSmart = pi->PI_smarts;
    if (pi->PI_customEntry)
	return printCustomRoutines[pi->PI_customEntry](ps);
    if (PrintResetPrinterAndWait(ps) ||
	PrSetPaperPath(ps, PS_UI_DATA(ps)->PUID_paperInput, 0))
	return TRUE;
    ps->PS_cursorPos.P_x = 0;
    ps->PS_cursorPos.P_y = 0;
    low = (Boolean) (ps->PS_mode == PM_GRAPHICS_LOW_RES);
    x = low ? LOW_RES_X_RES : HI_RES_X_RES;
    y = low ? LOW_RES_Y_RES : HI_RES_Y_RES;
    return SendCodeOut(ps, pr_codes_InitPrinter) ||
	SendCodeOut(ps, pr_codes_SetPrintResolution) ||
	PrintStreamWriteByte(ps, (byte) (y >> 8)) ||
	PrintStreamWriteByte(ps, (byte) y) ||
	PrintStreamWriteByte(ps, (byte) (x >> 8)) ||
	PrintStreamWriteByte(ps, (byte) x) ||
	SendCodeOut(ps, pr_codes_SetPrintingMethod) ||
	PrintStreamWriteByte(ps, low ? CANON_BJC_PRINT_QUALITY_DRAFT
				     : CANON_BJC_PRINT_QUALITY_STANDARD);
}
