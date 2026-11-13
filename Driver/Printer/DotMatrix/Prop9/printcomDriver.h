/*
 * printcomDriver.h -- the IBM Proprinter 9-pin driver's constants and
 * control codes for the PrintCom files (the C form of prop9Constant.def;
 * the codes are in prop9ControlCodes.c).
 */
#ifndef PRINTCOMDRIVER_H
#define PRINTCOMDRIVER_H

#define LO_RES_X_RES	60
#define LO_RES_Y_RES	72
#define LO_RES_BAND_HEIGHT	8
#define LO_RES_BUFF_HEIGHT	8
#define LO_RES_INTERLEAVE_FACTOR	1
#define MED_RES_X_RES	120
#define MED_RES_Y_RES	108
#define MED_RES_BAND_HEIGHT	12
#define MED_RES_BUFF_HEIGHT	4
#define MED_RES_INTERLEAVE_FACTOR	3
#define HI_RES_X_RES	240
#define HI_RES_Y_RES	216
#define HI_RES_BAND_HEIGHT	24
#define HI_RES_BUFF_HEIGHT	8
#define HI_RES_INTERLEAVE_FACTOR	3
#define PR_MARGIN_TRACTOR	18
#define PR_MARGIN_LEFT	18
#define PR_MARGIN_RIGHT	18
#define PR_MARGIN_TOP	36
#define PR_MARGIN_BOTTOM	36
#define PR_MAX_LINE_FEED	255
#define PR_INIT_WAIT_PERIOD	60

extern const byte pr_codes_ResetPrinter[], pr_codes_InitPrinter[],
    pr_codes_InitTextMode[], pr_codes_InitPaperLength[],
    pr_codes_FormFeed[], pr_codes_SetASCII7[], pr_codes_SetIBM437[],
    pr_codes_SetIBM850[], pr_codes_SetIBM860[], pr_codes_SetIBM863[],
    pr_codes_SetIBM865[], pr_codes_SetTab[], pr_codes_DoTab[],
    pr_codes_DoLineFeed[], pr_codes_DoMaxLineFeed[],
    pr_codes_Do1ScanlineFeed[], pr_codes_SetLoGraphics[],
    pr_codes_SetMedGraphics[], pr_codes_SetHiGraphics[],
    pr_codes_Set10Pitch[], pr_codes_Set12Pitch[],
    pr_codes_SetProportional[], pr_codes_Set10PitchRoman[],
    pr_codes_Set12PitchRoman[], pr_codes_Set15PitchRoman[],
    pr_codes_Set17PitchRoman[], pr_codes_Set20PitchRoman[],
    pr_codes_SetProportionalRoman[], pr_codes_Set10PitchSans[],
    pr_codes_Set12PitchSans[], pr_codes_Set15PitchSans[],
    pr_codes_Set17PitchSans[], pr_codes_Set20PitchSans[],
    pr_codes_SetProportionalSans[], pr_codes_SetCondensed[],
    pr_codes_SetSubscript[], pr_codes_SetSuperscript[], pr_codes_SetNLQ[],
    pr_codes_SetBold[], pr_codes_SetItalic[], pr_codes_SetUnderline[],
    pr_codes_SetOverline[], pr_codes_SetDblWidth[],
    pr_codes_SetDblHeight[], pr_codes_ResetCondensed[],
    pr_codes_ResetScript[], pr_codes_ResetNLQ[], pr_codes_ResetBold[],
    pr_codes_ResetItalic[], pr_codes_ResetUnderline[],
    pr_codes_ResetOverline[], pr_codes_ResetDblWidth[],
    pr_codes_ResetDblHeight[], pr_codes_SetColor[], pr_codes_SetYellow[],
    pr_codes_SetCyan[], pr_codes_SetMagenta[], pr_codes_SetBlack[];

#endif /* PRINTCOMDRIVER_H */
