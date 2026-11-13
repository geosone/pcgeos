/*
 * printcomDriver.h -- the DeskJet driver's constants and control codes
 * for the PrintCom files (the C form of deskjetConstant.def; the codes
 * are in deskjetControlCodes.c).
 */
#ifndef PRINTCOMDRIVER_H
#define PRINTCOMDRIVER_H

/* a count byte, then the bytes */
extern const byte pr_codes_ResetPrinter[], pr_codes_CursorPosition[],
    pr_codes_TransferNoGraphics[], pr_codes_EndGraphics[],
    pr_codes_SetPageLength[], pr_codes_MidPageLength[],
    pr_codes_FinishPageLength[], pr_codes_InitPrinter[],
    pr_codes_SetBold[], pr_codes_SetItalic[], pr_codes_SetUnderline[],
    pr_codes_ResetBold[], pr_codes_ResetItalic[], pr_codes_ResetUnderline[],
    pr_codes_SetASCII7[], pr_codes_SetIBM437[], pr_codes_SetIBM850[],
    pr_codes_SetRoman8[], pr_codes_SetLatin1[],
    pr_codes_SetResidentUpright[], pr_codes_SetResidentItalic[],
    pr_codes_SetResidentMedium[], pr_codes_SetResidentBold[];

/* zero-terminated, '#' the number (WriteNumCommand) */
extern const byte pr_codes_SetGraphicRes[], pr_codes_TIFFTransferGraphics[],
    pr_codes_TransferGraphics[], pr_codes_SetResidentFixed[],
    pr_codes_SetResidentProportional[], pr_codes_DuplexMode[],
    pr_codes_SetInputPath[];

/* the resolutions of the graphics modes (dpi), by PS_mode / 2 */
extern const word pr_graphic_Res_Values[3];

#endif /* PRINTCOMDRIVER_H */
