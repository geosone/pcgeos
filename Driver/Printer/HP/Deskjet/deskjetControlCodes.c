/*
 * deskjetControlCodes.c -- the control codes of the DeskJet driver (the
 * C port of deskjetControlCodes.asm).
 */
#include "printcom.h"
#include "printcomDriver.h"

const byte pr_codes_ResetPrinter[] = { 2, C_ESC, 'E' };
const byte pr_codes_CursorPosition[] = { 3, C_ESC, '*', 'p' };
const byte pr_codes_TransferNoGraphics[] = { 8, C_ESC, '*', 'b', '0', 'm',
    '1', 'W', 0 };
const byte pr_codes_EndGraphics[] = { 4, C_ESC, '*', 'r', 'B' };
const byte pr_codes_SetPageLength[] = { 5, C_ESC, '&', 'l', '8', 'd' };
					/* 8 lines an inch */
const byte pr_codes_MidPageLength[] = { 3, 'p', '2', 'e' };
					/* a top margin of 2 lines */
const byte pr_codes_FinishPageLength[] = { 6, 'F', C_ESC, '&', 'a', '0',
    'R' };				/* to the top margin */
const byte pr_codes_InitPrinter[] = { 13,
    C_ESC, '&', 'l', '0', 'o',		/* portrait */
    '0', 'L',				/* no perforation skip */
    C_ESC, '*', 'c', '0', 'F',		/* no downloaded fonts */
    C_SI };				/* the primary font */
const byte pr_codes_SetBold[] = { 5, C_ESC, '(', 's', '3', 'B' };
const byte pr_codes_SetItalic[] = { 5, C_ESC, '(', 's', '1', 'S' };
const byte pr_codes_SetUnderline[] = { 5, C_ESC, '&', 'd', '3', 'D' };
const byte pr_codes_ResetBold[] = { 5, C_ESC, '(', 's', '0', 'B' };
const byte pr_codes_ResetItalic[] = { 5, C_ESC, '(', 's', '0', 'S' };
const byte pr_codes_ResetUnderline[] = { 4, C_ESC, '&', 'd', '@' };
const byte pr_codes_SetASCII7[] = { 4, C_ESC, '(', '0', 'U' };
const byte pr_codes_SetIBM437[] = { 5, C_ESC, '(', '1', '0', 'U' };
const byte pr_codes_SetIBM850[] = { 5, C_ESC, '(', '1', '2', 'U' };
const byte pr_codes_SetRoman8[] = { 4, C_ESC, '(', '8', 'U' };
const byte pr_codes_SetLatin1[] = { 4, C_ESC, '(', '0', 'N' };
const byte pr_codes_SetResidentUpright[] = { 2, '0', 's' };
const byte pr_codes_SetResidentItalic[] = { 2, '1', 's' };
const byte pr_codes_SetResidentMedium[] = { 2, '0', 'b' };
const byte pr_codes_SetResidentBold[] = { 2, '3', 'b' };

const word pr_graphic_Res_Values[3] = { 75, 150, 300 };

const byte pr_codes_SetGraphicRes[] = { C_ESC, '*', 't', '#', 'R',
    C_ESC, '*', 'r', '1', 'A', 0 };
const byte pr_codes_TIFFTransferGraphics[] = { C_ESC, '*', 'b', '2', 'm',
    '#', 'W', 0 };
const byte pr_codes_TransferGraphics[] = { C_ESC, '*', 'b', '0', 'm', '#',
    'W', 0 };
const byte pr_codes_SetResidentFixed[] = { C_ESC, '(', 's', '0', 'p', '#',
    'h', 0 };
const byte pr_codes_SetResidentProportional[] = { C_ESC, '(', 's', '#',
    'p', 0 };
const byte pr_codes_DuplexMode[] = { C_ESC, '&', 'l', '#', 'S', 0 };
const byte pr_codes_SetInputPath[] = { C_ESC, '&', 'l', '#', 'H', 0 };

/* the driver table: an extended driver, its devices in printDevices
 * (Port32/ABI.md 7.6); deskjetManager.asm's */
DriverExtendedInfoStruct DriverTable = {
    { DRIVER_STRATEGY(PrintStrategy), DA_HAS_EXTENDED_INFO,
      DRIVER_TYPE_PRINTER },
    0
};
