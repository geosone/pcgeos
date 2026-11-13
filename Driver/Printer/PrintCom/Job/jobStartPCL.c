/*
 * jobStartPCL.c -- PrintStartJob of the PCL drivers (the C port of
 * jobStartPCL.asm without PCL4MODE): reset, the text state, the symbol
 * set, the paper path, the init codes and the page length in lines of
 * 1/8" (rounded up, at most 112) with a top margin of two lines.
 */
#include "printcom.h"
#include "printcomDriver.h"

/* PrintStartJob: dx:si (DR_dsSi) = the JobParameters (also after the
 * PState) */
Boolean PrintStartJob(PState *ps, DriverRegs *r)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;
    word lines;

    (void) r;
    ps->PS_printerType = pi->PI_type;
    ps->PS_printerSmart = pi->PI_smarts;
    /* PI_customEntry: no custom routines in the tables of these drivers
     * (dmdevinfo2c.py refuses them) */
    if (SendCodeOut(ps, pr_codes_ResetPrinter))
	return TRUE;
    ps->PS_asciiSpacing = 12;
    ps->PS_asciiStyle = 0;
    ps->PS_cursorPos.P_x = 0;
    ps->PS_cursorPos.P_y = 0;
    ps->PS_curFont.FE_stylesSet = 0;
    ps->PS_previousAttribute = 0xffff;
    ps->PS_curFont.FE_styles = 0xffff;
    PrintSetSymbolSet(ps);
    PrintLoadSymbolSet(ps);
    if (PrSetPaperPath(ps, PS_UI_DATA(ps)->PUID_paperInput,
		       PS_UI_DATA(ps)->PUID_paperOutput) ||
	SendCodeOut(ps, pr_codes_InitPrinter) ||
	SendCodeOut(ps, pr_codes_SetPageLength))
	return TRUE;
    lines = (word) ((ps->PS_customHeight + 8) / 9);
    if (lines > 112)
	lines = 112;
    if (HexToAsciiStreamWrite(ps, lines) ||
	SendCodeOut(ps, pr_codes_MidPageLength) ||
	HexToAsciiStreamWrite(ps, (word) (lines - 4)))
	return TRUE;
    return SendCodeOut(ps, pr_codes_FinishPageLength);
}
