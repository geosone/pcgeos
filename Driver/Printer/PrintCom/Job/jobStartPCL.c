/*
 * jobStartPCL.c -- PrintStartJob of the PCL drivers (the C port of
 * jobStartPCL.asm): the custom entry routine of the device (PCL 4), reset,
 * the copies in the printer (PCL 4: uncollated, the spooler then prints
 * one), the text state, the symbol set, the paper path, the init codes,
 * the soft fonts and macros deleted (PCL 4, PUID_initMemory) and the page
 * length in lines of 1/8" with a top margin of two lines: rounded up and
 * at most 112, or (PCL 4) rounded down and none for envelopes.
 */
#include "printcom.h"
#include "printcomDriver.h"
#ifdef PCL4MODE
#include <Internal/spoolInt.h>
#endif

/* PrintStartJob: dx:si (DR_dsSi) = the JobParameters (also after the
 * PState) */
Boolean PrintStartJob(PState *ps, DriverRegs *r)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;
    word lines;
#ifdef PCL4MODE
    JobParameters *jp = PS_JOB_PARAMS(ps);
    byte init;
#endif

    (void) r;
    ps->PS_printerType = pi->PI_type;
    ps->PS_printerSmart = pi->PI_smarts;
    /* the custom entry, then the standard start (BeginInit) */
    if (pi->PI_customEntry &&
	printCustomRoutines[pi->PI_customEntry](ps))
	return TRUE;
    if (SendCodeOut(ps, pr_codes_ResetPrinter))
	return TRUE;
#ifdef PCL4MODE
    if (jp->JP_spoolOpts & SO_COLLATE)
	lines = 1;
    else {
	lines = jp->JP_numCopies;
	jp->JP_numCopies = 1;
    }
    if (PrintSetCopies(ps, lines))
	return TRUE;
#endif
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
	SendCodeOut(ps, pr_codes_InitPrinter))
	return TRUE;
#ifdef PCL4MODE
    init = PS_UI_DATA(ps)->PUID_initMemory;
    if (init <= HPFC_DELETE_TEMP_SOFT_FONTS &&
	(WriteNumCommand(ps, pr_codes_FontControl, init) ||
	 WriteNumCommand(ps, pr_codes_MacroControl, (word) (init + 6))))
	return TRUE;		/* the macros: all or the temporary ones */
    if (jp->JP_paperSizeInfo.PSR_layout.PL_paper & PT_ENVELOPE)
	return FALSE;
#endif
    if (SendCodeOut(ps, pr_codes_SetPageLength))
	return TRUE;
#ifdef PCL4MODE
    lines = (word) (ps->PS_customHeight / 9);
#else
    lines = (word) ((ps->PS_customHeight + 8) / 9);
#endif
    if (lines > 112)
	lines = 112;
    if (HexToAsciiStreamWrite(ps, lines) ||
	SendCodeOut(ps, pr_codes_MidPageLength) ||
	HexToAsciiStreamWrite(ps, (word) (lines - 4)))
	return TRUE;
    return SendCodeOut(ps, pr_codes_FinishPageLength);
}
