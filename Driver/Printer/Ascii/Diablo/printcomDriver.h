/*
 * printcomDriver.h -- the Diablo daisy wheel driver's constants and control
 * codes for the PrintCom files (the C form of diabloConstant.def; the codes
 * are in diabloControlCodes.c).
 */
#ifndef PRINTCOMDRIVER_H
#define PRINTCOMDRIVER_H

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
    pr_codes_Set10PitchRoman[], pr_codes_Set12PitchRoman[],
    pr_codes_Set15PitchRoman[], pr_codes_Set17PitchRoman[],
    pr_codes_SetProportionalRoman[], pr_codes_SetBold[],
    pr_codes_SetItalic[], pr_codes_SetUnderline[], pr_codes_ResetBold[],
    pr_codes_ResetItalic[], pr_codes_ResetUnderline[];

#endif /* PRINTCOMDRIVER_H */
