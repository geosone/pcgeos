/*
 * printcomDriver.h -- the LaserJet (PCL 4) driver's constants and control
 * codes for the PrintCom files (the C form of pcl4Constant.def; the codes
 * are in pcl4ControlCodes.c).
 */
#ifndef PRINTCOMDRIVER_H
#define PRINTCOMDRIVER_H

#define PCL4MODE	1	/* the PCL 4 parts of the shared PCL files */

#define ENOUGH_BLANK	25	/* white bytes that split a raster line */

/* a count byte, then the bytes */
extern const byte pr_codes_ResetPrinter[], pr_codes_CursorPosition[],
    pr_codes_StartGraphics[], pr_codes_EndGraphics[],
    pr_codes_SetPageLength[], pr_codes_MidPageLength[],
    pr_codes_FinishPageLength[], pr_codes_SetLetterLength[],
    pr_codes_InitPrinter[], pr_codes_SICToPCL[], pr_codes_SICToPPDS[],
    pr_codes_PJLUEL[], pr_codes_PJLCRLF[], pr_codes_PJLEnterPCL[],
    pr_codes_PJLEnterPCL5[], pr_codes_PJLMsgPCL[], pr_codes_PJLMsgClear[],
    pr_codes_SetUnderline[], pr_codes_ResetUnderline[],
    pr_codes_SetASCII7[], pr_codes_SetIBM437[], pr_codes_SetIBM850[],
    pr_codes_SetRoman8[], pr_codes_SetWindows[], pr_codes_SetVentura[],
    pr_codes_SetLatin1[], pr_codes_SetResidentUpright[],
    pr_codes_SetResidentItalic[], pr_codes_SetResidentMedium[],
    pr_codes_SetResidentBold[];

/* zero-terminated, '#' the number (WriteNumCommand) */
extern const byte pr_codes_SetGraphicRes[], pr_codes_TransferGraphics[],
    pr_codes_StartAndTransferGraphics[], pr_codes_SelectFont[],
    pr_codes_SelectChar[], pr_codes_FontControl[], pr_codes_SetFontID[],
    pr_codes_MacroControl[], pr_codes_SetResidentFixed[],
    pr_codes_SetResidentProportional[], pr_codes_SendFont[],
    pr_codes_SendChar[], pr_codes_ContinueChar[], pr_codes_AdjustInX[],
    pr_codes_AdjustInY[], pr_codes_RightRelMove[], pr_codes_NumCopies[],
    pr_codes_DuplexMode[], pr_codes_SetInputPath[];

/* the resolutions of the graphics modes (dpi), by PS_mode / 2 */
extern const word pr_graphic_Res_Values[3];

#endif /* PRINTCOMDRIVER_H */
