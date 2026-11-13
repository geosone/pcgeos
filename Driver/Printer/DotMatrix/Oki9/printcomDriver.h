/*
 * printcomDriver.h -- the Okidata 9-pin driver's constants and control
 * codes for the PrintCom files (the C form of oki9Constant.def; the codes
 * are in oki9ControlCodes.c).
 */
#ifndef PRINTCOMDRIVER_H
#define PRINTCOMDRIVER_H

#define LO_RES_X_RES	72
#define LO_RES_Y_RES	72
#define LO_RES_BAND_HEIGHT	7
#define LO_RES_BUFF_HEIGHT	7
#define LO_RES_INTERLEAVE_FACTOR	1
#define HI_RES_X_RES	72
#define HI_RES_Y_RES	144
#define HI_RES_BAND_HEIGHT	14
#define HI_RES_BUFF_HEIGHT	7
#define HI_RES_INTERLEAVE_FACTOR	2
#define PR_MARGIN_TRACTOR	18
#define PR_MARGIN_LEFT	18
#define PR_MARGIN_RIGHT	18
#define PR_MARGIN_TOP	36
#define PR_MARGIN_BOTTOM	36
#define PR_MAX_LINE_FEED	127
#define PR_INIT_WAIT_PERIOD	60

extern const byte pr_codes_ResetPrinter[], pr_codes_InitPrinter[],
    pr_codes_InitTextMode[], pr_codes_InitPaperLength[],
    pr_codes_FormFeed[], pr_codes_SetTab[], pr_codes_DoTab[],
    pr_codes_SetLineFeed[], pr_codes_SetMaxLineFeed[],
    pr_codes_Do1ScanlineFeed[], pr_codes_EnterGraphics[],
    pr_codes_ExitGraphics[], pr_codes_Set5Pitch[], pr_codes_Set6Pitch[],
    pr_codes_Set10Pitch[], pr_codes_Set12Pitch[], pr_codes_Set17Pitch[],
    pr_codes_SetProportional[], pr_codes_SetSubscript[],
    pr_codes_SetSuperscript[], pr_codes_SetNLQ[], pr_codes_SetBold[],
    pr_codes_SetItalic[], pr_codes_SetUnderline[],
    pr_codes_ResetSuperscript[], pr_codes_ResetSubscript[],
    pr_codes_ResetNLQ[], pr_codes_ResetBold[], pr_codes_ResetItalic[],
    pr_codes_ResetUnderline[];

#endif /* PRINTCOMDRIVER_H */
