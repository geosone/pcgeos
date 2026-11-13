/*
 * jobStartDotMatrix.c -- PrintStartJob (the C port of
 * jobStartDotMatrix.asm): the device's custom entry routine instead of
 * the standard start, or reset, paper path, the text state, the default
 * font with the styles cleared, the init codes.
 */
#include "printcom.h"
#include "printcomDriver.h"

/* PrintStartJob: dx:si (DR_dsSi) = the JobParameters (also after the
 * PState) */
Boolean PrintStartJob(PState *ps, DriverRegs *r)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;

    (void) r;
    ps->PS_printerType = pi->PI_type;
    ps->PS_printerSmart = pi->PI_smarts;
    if (pi->PI_customEntry)
	return printCustomRoutines[pi->PI_customEntry](ps);
    PrintResetPrinterAndWait(ps);
    PrSetPaperPath(ps, PS_UI_DATA(ps)->PUID_paperInput, 0);
    ps->PS_asciiSpacing = 12;
    ps->PS_cursorPos.P_x = 0;
    ps->PS_cursorPos.P_y = 0;
    ps->PS_previousAttribute = 0xffff;
    ps->PS_asciiStyle = (PrintTextStyle) 0xffff;	/* all: cleared below */
    ps->PS_curFont.FE_symbolSet = (PrinterSymbolSet) 0xffff;
    PrSetFont(ps, FID_DTC_URW_ROMAN, 12, TP_10_PITCH);
    PrSetStyles(ps, 0);
    if (SendCodeOut(ps, pr_codes_InitPrinter))
	return TRUE;
    if (ps->PS_mode >= PM_FIRST_TEXT_MODE &&
	SendCodeOut(ps, pr_codes_InitTextMode))
	return TRUE;
    return FALSE;
}
