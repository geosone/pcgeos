/*
 * printcomDriver.h -- the HP DeskJet 500C driver's constants and control
 * codes for the PrintCom files (the C form of dj500cConstant.def; the codes
 * are in dj500cControlCodes.c).
 */
#ifndef PRINTCOMDRIVER_H
#define PRINTCOMDRIVER_H

#define LO_RES_X_RES	75
#define LO_RES_Y_RES	75
#define LO_RES_BAND_HEIGHT	1
#define LO_RES_BUFF_HEIGHT	2
#define LO_RES_INTERLEAVE_FACTOR	1
#define MED_RES_X_RES	150
#define MED_RES_Y_RES	150
#define MED_RES_BAND_HEIGHT	1
#define MED_RES_BUFF_HEIGHT	2
#define MED_RES_INTERLEAVE_FACTOR	1
#define HI_RES_X_RES	300
#define HI_RES_Y_RES	300
#define HI_RES_BAND_HEIGHT	1
#define HI_RES_BUFF_HEIGHT	2
#define HI_RES_INTERLEAVE_FACTOR	1
#define PR_MARGIN_TRACTOR	18
#define PR_MARGIN_LEFT	18
#define PR_MARGIN_RIGHT	18
#define PR_MARGIN_TOP	36
#define PR_MARGIN_BOTTOM	36

extern const byte pr_codes_ResetPrinter[], pr_codes_CursorPosition[],
    pr_codes_TransferNoGraphics[], pr_codes_Do1ScanlineFeed[],
    pr_codes_EndGraphics[], pr_codes_SetPageLength[],
    pr_codes_MidPageLength[], pr_codes_FinishPageLength[],
    pr_codes_InitPrinter[], pr_codes_PJLEntryAndResetNoNeg[],
    pr_codes_PJLExit[], pr_codes_SetASCII7[], pr_codes_SetIBM437[],
    pr_codes_SetIBM850[], pr_codes_SetRoman8[], pr_codes_SetLatin1[],
    pr_codes_SetGraphicRes[], pr_codes_TIFFTransferGraphics[],
    pr_codes_TransferGraphics[], pr_codes_DuplexMode[],
    pr_codes_SetInputPath[];
extern const word pr_graphic_Res_Values[3];

#endif /* PRINTCOMDRIVER_H */
