/*
 * printcomDriver.h -- the Toshiba 24-pin driver's constants and control
 * codes for the PrintCom files (the C form of toshiba24Constant.def; the
 * codes are in toshiba24ControlCodes.c).
 */
#ifndef PRINTCOMDRIVER_H
#define PRINTCOMDRIVER_H

#define MED_RES_X_RES	180
#define MED_RES_Y_RES	180
#define MED_RES_BAND_HEIGHT	19
#define MED_RES_BUFF_HEIGHT	19
#define MED_RES_INTERLEAVE_FACTOR	1
#define HI_RES_X_RES	360
#define HI_RES_Y_RES	180
#define HI_RES_BAND_HEIGHT	19
#define HI_RES_BUFF_HEIGHT	19
#define HI_RES_INTERLEAVE_FACTOR	1
#define PR_MARGIN_TRACTOR	18
#define PR_MARGIN_LEFT	18
#define PR_MARGIN_RIGHT	18
#define PR_MARGIN_TOP	36
#define PR_MARGIN_BOTTOM	36
#define PR_MAX_LINE_FEED	99
#define PR_X_CURSOR_INT	1
#define PR_X_CURSOR_FRAC	43909
#define PR_COND_X_CURSOR_INT	2
#define PR_COND_X_CURSOR_FRAC	50972
#define PR_INIT_WAIT_PERIOD	60

extern const byte pr_codes_ResetPrinter[], pr_codes_InitPrinter[],
    pr_codes_InitTextMode[], pr_codes_DefeatPaperOut[],
    pr_codes_InitPaperLength[], pr_codes_ASFControl[],
    pr_codes_EnableASF[], pr_codes_DisableASF[], pr_codes_AbsTab[],
    pr_codes_SetMaxLineFeed[], pr_codes_SetLineFeed[],
    pr_codes_GraphFeed[], pr_codes_SetMedGraphics[],
    pr_codes_SetHiGraphics[], pr_codes_Set10PitchDraft[],
    pr_codes_Set12PitchDraft[], pr_codes_Set15PitchDraft[],
    pr_codes_Set10PitchRoman[], pr_codes_Set12PitchRoman[],
    pr_codes_Set15PitchRoman[], pr_codes_Set17PitchRoman[],
    pr_codes_SetProportionalRoman[], pr_codes_SetCondensed[],
    pr_codes_SetNLQ[], pr_codes_SetBold[], pr_codes_SetItalic[],
    pr_codes_SetUnderline[], pr_codes_SetDblWidth[], pr_codes_SetShadow[],
    pr_codes_ResetCondensed[], pr_codes_ResetNLQ[], pr_codes_ResetBold[],
    pr_codes_ResetItalic[], pr_codes_ResetUnderline[],
    pr_codes_ResetDblWidth[], pr_codes_ResetShadow[];

#endif /* PRINTCOMDRIVER_H */
