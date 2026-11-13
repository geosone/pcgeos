/*
 * printcomDriver.h -- the C.Itoh 9-pin driver's constants and control codes
 * for the PrintCom files (the C form of citoh9Constant.def; the codes are
 * in citoh9ControlCodes.c).
 */
#ifndef PRINTCOMDRIVER_H
#define PRINTCOMDRIVER_H

#define LO_RES_X_RES	80
#define LO_RES_Y_RES	72
#define LO_RES_BAND_HEIGHT	8
#define LO_RES_BUFF_HEIGHT	8
#define LO_RES_INTERLEAVE_FACTOR	1
#define HI_RES_X_RES	160
#define HI_RES_Y_RES	144
#define HI_RES_BAND_HEIGHT	16
#define HI_RES_BUFF_HEIGHT	8
#define HI_RES_INTERLEAVE_FACTOR	2
#define PR_MARGIN_TRACTOR	18
#define PR_MARGIN_LEFT	18
#define PR_MARGIN_RIGHT	18
#define PR_MARGIN_TOP	36
#define PR_MARGIN_BOTTOM	36
#define PR_MAX_LINE_FEED	99
#define PR_INIT_WAIT_PERIOD	60

extern const byte pr_codes_ResetPrinter[], pr_codes_InitPrinter[],
    pr_codes_InitTextMode[], pr_codes_AbsPos[], pr_codes_SetLineFeed[],
    pr_codes_SetMaxLineFeed[], pr_codes_Do1ScanlineFeed[],
    pr_codes_InitPaperLength[], pr_codes_ASFControl[],
    pr_codes_EnableASF[], pr_codes_DisableASF[], pr_codes_DefeatPaperOut[],
    pr_codes_SetLoGraphics[], pr_codes_SetHiGraphics[],
    pr_codes_Set10Pitch[], pr_codes_Set12Pitch[],
    pr_codes_SetProportional[], pr_codes_Set17Pitch[],
    pr_codes_SetCondensed[], pr_codes_SetSubscript[],
    pr_codes_SetSuperscript[], pr_codes_SetNLQ[], pr_codes_SetBold[],
    pr_codes_SetItalic[], pr_codes_SetUnderline[], pr_codes_SetDblWidth[],
    pr_codes_ResetCondensed[], pr_codes_ResetScript[], pr_codes_ResetNLQ[],
    pr_codes_ResetBold[], pr_codes_ResetItalic[],
    pr_codes_ResetUnderline[], pr_codes_ResetDblWidth[],
    pr_codes_SetStrikeThru[], pr_codes_SetDblHeight[],
    pr_codes_ResetStrikeThru[], pr_codes_ResetDblHeight[],
    pr_codes_SetColor[], pr_codes_SetYellow[], pr_codes_SetCyan[],
    pr_codes_SetMagenta[], pr_codes_SetBlack[];

#endif /* PRINTCOMDRIVER_H */
