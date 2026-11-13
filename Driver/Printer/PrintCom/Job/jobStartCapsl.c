/*
 * jobStartCapsl.c -- PrintStartJob (the C port of jobStartCapsl.asm): a
 * Canon CaPSL laser printer into its ISO mode, reset; the copies (the
 * printer makes them unless they are collated), the paper path, the
 * text state, the paper size (letter, legal, A4 and B5 by their code, any
 * other by its length and width in dots, which the printer asks the user
 * for), the init codes (full paint mode, 1/300" units, the IBM character
 * sets, scalable fonts) and the symbol set IBM 437.
 */
#include "printcom.h"
#include "printcomDriver.h"

/* PrintStartJob: dx:si (DR_dsSi) = the JobParameters (also after the
 * PState) */
Boolean PrintStartJob(PState *ps, DriverRegs *r)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;
    JobParameters *jp = PS_JOB_PARAMS(ps);
    const byte *paper;
    word copies;

    (void) r;
    ps->PS_printerType = pi->PI_type;
    ps->PS_printerSmart = pi->PI_smarts;
    if (pi->PI_customEntry)
	return printCustomRoutines[pi->PI_customEntry](ps);
    if (SendCodeOut(ps, pr_codes_ResetPrinter))
	return TRUE;
    if (jp->JP_spoolOpts & SO_COLLATE)
	copies = 1;
    else {
	copies = jp->JP_numCopies;
	jp->JP_numCopies = 1;
    }
    if (PrintSetCopies(ps, copies) ||
	PrSetPaperPath(ps, PS_UI_DATA(ps)->PUID_paperInput,
		       PS_UI_DATA(ps)->PUID_paperOutput))
	return TRUE;
    ps->PS_asciiSpacing = 12;
    ps->PS_asciiStyle = 0;
    ps->PS_cursorPos.P_x = 0;
    ps->PS_cursorPos.P_y = 0;
    ps->PS_previousAttribute = 0xffff;
    switch (ps->PS_customHeight) {
    case 792:	paper = pr_codes_InitPaperLetter; break;
    case 1008:	paper = pr_codes_InitPaperLegal; break;
    case 842:	paper = pr_codes_InitPaperA4; break;
    case 709:	paper = pr_codes_InitPaperB5; break;
    default:	paper = NULL; break;
    }
    if (paper) {
	if (SendCodeOut(ps, paper))
	    return TRUE;
    } else if (SendCodeOut(ps, pr_codes_InitPaperCustom) ||
	       HexToAsciiStreamWrite(ps, PrConvertToDriverCoordinates(
		   (WWFixedAsDWord) ps->PS_customHeight << 16)) ||
	       PrintStreamWriteByte(ps, ';') ||
	       HexToAsciiStreamWrite(ps, PrConvertToDriverCoordinates(
		   (WWFixedAsDWord) ps->PS_customWidth << 16)))
	return TRUE;
    if (SendCodeOut(ps, pr_codes_InitPrinter))
	return TRUE;
    ps->PS_curFont.FE_symbolSet = PSS_IBM437;
    PS_UI_DATA(ps)->PUID_symbolSet = PSS_IBM437;
    PS_UI_DATA(ps)->PUID_countryCode = PCC_USA;
    PrintSetSymbolSet(ps);
    PrintLoadSymbolSet(ps);
    return FALSE;
}
