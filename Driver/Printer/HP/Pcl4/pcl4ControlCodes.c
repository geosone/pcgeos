/*
 * pcl4ControlCodes.c -- the control codes of the LaserJet (PCL 4) driver
 * (the C port of pcl4ControlCodes.asm) and its driver table
 * (pcl4Manager.asm).
 */
#include "printcom.h"
#include "printcomDriver.h"

const byte pr_codes_ResetPrinter[] = { 2, C_ESC, 'E' };
const byte pr_codes_CursorPosition[] = { 3, C_ESC, '*', 'p' };
const byte pr_codes_StartGraphics[] = { 5, C_ESC, '*', 'r', '1', 'A' };
const byte pr_codes_EndGraphics[] = { 4, C_ESC, '*', 'r', 'B' };
const byte pr_codes_SetPageLength[] = { 5, C_ESC, '&', 'l', '8', 'd' };
					/* 8 lines an inch */
const byte pr_codes_MidPageLength[] = { 3, 'p', '2', 'e' };
					/* a top margin of 2 lines */
const byte pr_codes_FinishPageLength[] = { 6, 'F', C_ESC, '&', 'a', '0',
    'R' };				/* to the top margin */
const byte pr_codes_SetLetterLength[] = { 23,
    C_ESC, '&', 'l', '8', 'd',		/* 8 lines an inch */
    '8', '8', 'p', '2', 'e',		/* 88 lines, a top margin of 2 */
    '8', '4', 'F', C_ESC, '&', 'a', '0', 'R',	/* 84 lines of text */
    C_ESC, '&', 'l', '6', 'd' };	/* back to 6 lines an inch */
const byte pr_codes_InitPrinter[] = { 8,
    C_ESC, '&', 'l', '0', 'o',		/* portrait */
    '0', 'L',				/* no perforation skip */
    C_SI };				/* the primary font */
const byte pr_codes_SICToPCL[] = { 8, C_ESC, '[', 'K', 3, 0, 5, 0x31, 2 };
const byte pr_codes_SICToPPDS[] = { 8, C_ESC, '[', 'K', 3, 0, 1, 0x31, 1 };
const byte pr_codes_PJLUEL[] = { 9, C_ESC, '%', '-', '1', '2', '3', '4',
    '5', 'X' };
const byte pr_codes_PJLCRLF[] = { 6, '@', 'P', 'J', 'L', C_CR, C_LF };
const byte pr_codes_PJLEnterPCL[] = { 27, '@', 'P', 'J', 'L', ' ', 'E', 'N',
    'T', 'E', 'R', ' ', 'L', 'A', 'N', 'G', 'U', 'A', 'G', 'E', ' ', '=',
    ' ', 'P', 'C', 'L', C_CR, C_LF };
const byte pr_codes_PJLEnterPCL5[] = { 28, '@', 'P', 'J', 'L', ' ', 'E',
    'N', 'T', 'E', 'R', ' ', 'L', 'A', 'N', 'G', 'U', 'A', 'G', 'E', ' ',
    '=', ' ', 'P', 'C', 'L', '5', C_CR, C_LF };
const byte pr_codes_PJLMsgPCL[] = { 38, '@', 'P', 'J', 'L', ' ', 'R', 'D',
    'Y', 'M', 'S', 'G', ' ', 'D', 'I', 'S', 'P', 'L', 'A', 'Y', ' ', '=',
    ' ', '"', 'G', 'E', 'O', 'S', ' ', 'P', 'C', 'L', ' ', 'J', 'O', 'B',
    '"', C_CR, C_LF };
const byte pr_codes_PJLMsgClear[] = { 26, '@', 'P', 'J', 'L', ' ', 'R',
    'D', 'Y', 'M', 'S', 'G', ' ', 'D', 'I', 'S', 'P', 'L', 'A', 'Y', ' ',
    '=', ' ', '"', '"', C_CR, C_LF };
const byte pr_codes_SetUnderline[] = { 5, C_ESC, '&', 'd', '3', 'D' };
const byte pr_codes_ResetUnderline[] = { 4, C_ESC, '&', 'd', '@' };
const byte pr_codes_SetASCII7[] = { 4, C_ESC, '(', '0', 'U' };
const byte pr_codes_SetIBM437[] = { 5, C_ESC, '(', '1', '0', 'U' };
const byte pr_codes_SetIBM850[] = { 5, C_ESC, '(', '1', '2', 'U' };
const byte pr_codes_SetRoman8[] = { 4, C_ESC, '(', '8', 'U' };
const byte pr_codes_SetWindows[] = { 4, C_ESC, '(', '9', 'U' };
const byte pr_codes_SetVentura[] = { 5, C_ESC, '(', '1', '3', 'J' };
const byte pr_codes_SetLatin1[] = { 4, C_ESC, '(', '0', 'N' };
const byte pr_codes_SetResidentUpright[] = { 2, '0', 's' };
const byte pr_codes_SetResidentItalic[] = { 2, '1', 's' };
const byte pr_codes_SetResidentMedium[] = { 2, '0', 'b' };
const byte pr_codes_SetResidentBold[] = { 2, '3', 'b' };

const word pr_graphic_Res_Values[3] = { 75, 150, 300 };

const byte pr_codes_SetGraphicRes[] = { C_ESC, '*', 't', '#', 'R', 0 };
const byte pr_codes_TransferGraphics[] = { C_ESC, '*', 'b', '#', 'W', 0 };
const byte pr_codes_StartAndTransferGraphics[] = { C_ESC, '*', 'r', '1',
    'A', C_ESC, '*', 'b', '#', 'W', 0 };
const byte pr_codes_SelectFont[] = { C_ESC, '(', '#', 'X', 0 };
const byte pr_codes_SelectChar[] = { C_ESC, '*', 'c', '#', 'E', 0 };
const byte pr_codes_FontControl[] = { C_ESC, '*', 'c', '#', 'F', 0 };
const byte pr_codes_SetFontID[] = { C_ESC, '*', 'c', '#', 'D', 0 };
const byte pr_codes_MacroControl[] = { C_ESC, '&', 'f', '#', 'X', 0 };
const byte pr_codes_SetResidentFixed[] = { C_ESC, '(', 's', '0', 'p', '#',
    'h', 0 };
const byte pr_codes_SetResidentProportional[] = { C_ESC, '(', 's', '#',
    'p', 0 };
const byte pr_codes_SendFont[] = { C_ESC, ')', 's', '#', 'W', 0 };
const byte pr_codes_SendChar[] = { C_ESC, '(', 's', '#', 'W', 0 };
/* the format and continuation bytes of a character's continuation */
const byte pr_codes_ContinueChar[] = { C_ESC, '(', 's', '#', 'W', 4, 1, 0 };
const byte pr_codes_AdjustInX[] = { C_ESC, '*', 'p', '#', 'X', 0 };
const byte pr_codes_AdjustInY[] = { C_ESC, '*', 'p', '#', 'Y', 0 };
const byte pr_codes_RightRelMove[] = { C_ESC, '*', 'p', '+', '#', 'X', 0 };
const byte pr_codes_NumCopies[] = { C_ESC, '&', 'l', '#', 'X', 0 };
const byte pr_codes_DuplexMode[] = { C_ESC, '&', 'l', '#', 'S', 0 };
const byte pr_codes_SetInputPath[] = { C_ESC, '&', 'l', '#', 'H', 0 };

/* the driver table: an extended driver, its devices in printDevices
 * (Port32/ABI.md 7.6) */
DriverExtendedInfoStruct DriverTable = {
    { DRIVER_STRATEGY(PrintStrategy), DA_HAS_EXTENDED_INFO,
      DRIVER_TYPE_PRINTER },
    0
};
