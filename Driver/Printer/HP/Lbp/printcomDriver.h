/*
 * printcomDriver.h -- the Canon CaPSL driver's constants and control codes
 * for the PrintCom files (the C form of lbpConstant.def; the codes are in
 * lbpControlCodes.c).
 */
#ifndef PRINTCOMDRIVER_H
#define PRINTCOMDRIVER_H

#define LO_RES_X_RES	75
#define LO_RES_Y_RES	75
#define LO_RES_BAND_HEIGHT	8
#define LO_RES_BUFF_HEIGHT	8
#define LO_RES_INTERLEAVE_FACTOR	1
#define MED_RES_X_RES	150
#define MED_RES_Y_RES	150
#define MED_RES_BAND_HEIGHT	16
#define MED_RES_BUFF_HEIGHT	16
#define MED_RES_INTERLEAVE_FACTOR	1
#define HI_RES_X_RES	300
#define HI_RES_Y_RES	300
#define HI_RES_BAND_HEIGHT	32
#define HI_RES_BUFF_HEIGHT	32
#define HI_RES_INTERLEAVE_FACTOR	1
#define PR_MARGIN_LEFT	12
#define PR_MARGIN_RIGHT	23
#define PR_MARGIN_TOP	32
#define PR_MARGIN_BOTTOM	15

extern const byte pr_codes_CSIcode[], pr_codes_ResetPrinter[],
    pr_codes_InitPaperLetter[], pr_codes_InitPaperLegal[],
    pr_codes_InitPaperA4[], pr_codes_InitPaperB5[],
    pr_codes_InitPaperCustom[], pr_codes_InitPrinter[],
    pr_codes_SetGraphics[], pr_codes_SetDutch[], pr_codes_SetSwiss[],
    pr_codes_SetCourier[], pr_codes_SetPitchStart[],
    pr_codes_SetPitchEnd[], pr_codes_SetSizeStart[], pr_codes_SetSizeEnd[],
    pr_codes_Set10Pitch[], pr_codes_Set12Pitch[], pr_codes_Set15Pitch[],
    pr_codes_SetProportional[], pr_codes_SetBold[], pr_codes_SetItalic[],
    pr_codes_SetUnderline[], pr_codes_SetShadow[], pr_codes_SetOutline[],
    pr_codes_SetReverse[], pr_codes_SetDblWidth[], pr_codes_ResetBold[],
    pr_codes_ResetItalic[], pr_codes_ResetUnderline[],
    pr_codes_ResetShadow[], pr_codes_ResetOutline[],
    pr_codes_ResetReverse[], pr_codes_ResetDblWidth[];

#endif /* PRINTCOMDRIVER_H */
