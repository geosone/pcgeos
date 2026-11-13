/*
 * jobStartDefeatPaperout.c -- PrintStartJob (the C port of
 * jobStartDefeatPaperout.asm): reset, paper path, the text state, the
 * default font, the init codes; on cut sheets the paper-out sensor off.
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
    /* PI_customEntry: no custom routines in the tables of these drivers
     * (dmdevinfo2c.py refuses them) */
    PrintResetPrinterAndWait(ps);
    PrSetPaperPath(ps, PS_UI_DATA(ps)->PUID_paperInput, 0);
    ps->PS_asciiSpacing = 12;
    ps->PS_asciiStyle = 0;
    ps->PS_cursorPos.P_x = 0;
    ps->PS_cursorPos.P_y = 0;
    ps->PS_previousAttribute = 0xffff;
    ps->PS_curFont.FE_symbolSet = (PrinterSymbolSet) 0xffff;
    PrSetFont(ps, FID_DTC_URW_ROMAN, 12, TP_10_PITCH);
    if (SendCodeOut(ps, pr_codes_InitPrinter))
	return TRUE;
    if (ps->PS_mode >= PM_FIRST_TEXT_MODE &&
	SendCodeOut(ps, pr_codes_InitTextMode))
	return TRUE;
    if (ps->PS_paperInput & PIO_TRACTOR)
	return FALSE;
    return SendCodeOut(ps, pr_codes_DefeatPaperOut);
}
