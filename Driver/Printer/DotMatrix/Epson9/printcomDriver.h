/*
 * printcomDriver.h -- the Epson 9-pin driver's constants and control
 * codes for the PrintCom files (the C form of epson9Constant.def; the
 * codes are in epson9ControlCodes.c).
 */
#ifndef PRINTCOMDRIVER_H
#define PRINTCOMDRIVER_H

#define LO_RES_BAND_HEIGHT		8
#define MED_RES_BAND_HEIGHT		12
#define HI_RES_BAND_HEIGHT		24
#define HI_RES_INTERLEAVE_FACTOR	3

#define PR_MAX_LINE_FEED	255	/* the most ESC J feeds */
#define PR_X_CURSOR_INT		0	/* 60/72, WWFixed: the X position */
#define PR_X_CURSOR_FRAC	54395
#define PR_INIT_WAIT_PERIOD	60	/* the printer's reset (ticks) */

extern const byte pr_codes_ResetPrinter[], pr_codes_InitPrinter[],
    pr_codes_InitTextMode[], pr_codes_DefeatPaperOut[],
    pr_codes_SetCountry[], pr_codes_InitPaperLength[], pr_codes_FormFeed[],
    pr_codes_ASFControl[], pr_codes_EnableASF[], pr_codes_DisableASF[],
    pr_codes_AbsPos[], pr_codes_SetTab[], pr_codes_DoTab[],
    pr_codes_DoLineFeed[], pr_codes_DoMaxLineFeed[],
    pr_codes_Do1ScanlineFeed[], pr_codes_SetLoGraphics[],
    pr_codes_SetMedGraphics[], pr_codes_SetHiGraphics[],
    pr_codes_Set10PitchRoman[], pr_codes_Set12PitchRoman[],
    pr_codes_SetProportionalRoman[], pr_codes_Set10PitchSans[],
    pr_codes_Set12PitchSans[], pr_codes_SetProportionalSans[],
    pr_codes_SetCondensed[], pr_codes_SetSubscript[],
    pr_codes_SetSuperscript[], pr_codes_SetNLQ[], pr_codes_SetBold[],
    pr_codes_SetItalic[], pr_codes_SetUnderline[], pr_codes_SetDblWidth[],
    pr_codes_SetDblHeight[], pr_codes_ResetCondensed[],
    pr_codes_ResetScript[], pr_codes_ResetNLQ[], pr_codes_ResetBold[],
    pr_codes_ResetItalic[], pr_codes_ResetUnderline[],
    pr_codes_ResetDblWidth[], pr_codes_ResetDblHeight[],
    pr_codes_SetColor[], pr_codes_SetYellow[], pr_codes_SetCyan[],
    pr_codes_SetMagenta[], pr_codes_SetBlack[];

#endif /* PRINTCOMDRIVER_H */
