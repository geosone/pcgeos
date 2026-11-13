/*
 * epson24ControlCodes.c -- the control codes of the Epson 24-pin driver
 * (the C port of epson24ControlCodes.asm): a count byte, then the bytes.
 */
#include "printcom.h"
#include "printcomDriver.h"

const byte pr_codes_ResetPrinter[] = { 2, C_ESC, '@' };
const byte pr_codes_InitPrinter[] = { 13,
    C_ESC, 'O',			/* no perforation skipping */
    C_ESC, '6',			/* print 80h to 9fh */
    C_ESC, 't', 1,		/* the foreign character table */
    C_ESC, 'U', 1,		/* unidirectional */
    C_ESC, 'r', 0 };		/* black */
const byte pr_codes_InitTextMode[] = { 3, C_ESC, 'U', 0 };
const byte pr_codes_DefeatPaperOut[] = { 2, C_ESC, '8' };
const byte pr_codes_SetCountry[] = { 2, C_ESC, 'R' };
const byte pr_codes_InitPaperLength[] = { 4, C_ESC, 'C', 0, 22 };
const byte pr_codes_FormFeed[] = { 5, C_ESC, '3', 3, C_ESC, 'C' };
const byte pr_codes_ASFControl[] = { 2, C_ESC, C_EM };
const byte pr_codes_EnableASF[] = { 3, C_ESC, C_EM, 4 };
const byte pr_codes_DisableASF[] = { 3, C_ESC, C_EM, 0 };
const byte pr_codes_AbsPos[] = { 2, C_ESC, '$' };
const byte pr_codes_DoLineFeed[] = { 2, C_ESC, 'J' };
const byte pr_codes_DoMaxLineFeed[] = { 3, C_ESC, 'J', PR_MAX_LINE_FEED };
const byte pr_codes_SetLoGraphics[] = { 4, C_CR, C_ESC, '*', 0 };
const byte pr_codes_SetMedGraphics[] = { 4, C_CR, C_ESC, '*', 39 };
const byte pr_codes_SetHiGraphics[] = { 4, C_CR, C_ESC, '*', 40 };
const byte pr_codes_Set10PitchRoman[] = { 8, C_ESC, 'k', 0, C_ESC, 'p', 0,
    C_ESC, 'P' };
const byte pr_codes_Set12PitchRoman[] = { 8, C_ESC, 'k', 0, C_ESC, 'p', 0,
    C_ESC, 'M' };
const byte pr_codes_Set15PitchRoman[] = { 8, C_ESC, 'k', 0, C_ESC, 'p', 0,
    C_ESC, 'g' };
const byte pr_codes_SetProportionalRoman[] = { 6, C_ESC, 'k', 0, C_ESC,
    'p', 1 };
const byte pr_codes_Set10PitchSans[] = { 8, C_ESC, 'k', 1, C_ESC, 'p', 0,
    C_ESC, 'P' };
const byte pr_codes_Set12PitchSans[] = { 8, C_ESC, 'k', 1, C_ESC, 'p', 0,
    C_ESC, 'M' };
const byte pr_codes_Set15PitchSans[] = { 8, C_ESC, 'k', 1, C_ESC, 'p', 0,
    C_ESC, 'g' };
const byte pr_codes_SetProportionalSans[] = { 6, C_ESC, 'k', 1, C_ESC,
    'p', 1 };
const byte pr_codes_SetCondensed[] = { 1, C_SI };
const byte pr_codes_SetSubscript[] = { 3, C_ESC, 'S', 1 };
const byte pr_codes_SetSuperscript[] = { 3, C_ESC, 'S', 0 };
const byte pr_codes_SetNLQ[] = { 3, C_ESC, 'x', 1 };
const byte pr_codes_SetBold[] = { 4, C_ESC, 'E', C_ESC, 'G' };
const byte pr_codes_SetItalic[] = { 2, C_ESC, '4' };
const byte pr_codes_SetUnderline[] = { 3, C_ESC, '-', 1 };
const byte pr_codes_SetDblWidth[] = { 3, C_ESC, 'W', 1 };
const byte pr_codes_SetDblHeight[] = { 3, C_ESC, 'w', 1 };
const byte pr_codes_ResetCondensed[] = { 1, C_DC2 };
const byte pr_codes_ResetScript[] = { 2, C_ESC, 'T' };
const byte pr_codes_ResetNLQ[] = { 3, C_ESC, 'x', 0 };
const byte pr_codes_ResetBold[] = { 4, C_ESC, 'F', C_ESC, 'H' };
const byte pr_codes_ResetItalic[] = { 2, C_ESC, '5' };
const byte pr_codes_ResetUnderline[] = { 3, C_ESC, '-', 0 };
const byte pr_codes_ResetDblWidth[] = { 3, C_ESC, 'W', 0 };
const byte pr_codes_ResetDblHeight[] = { 3, C_ESC, 'w', 0 };
const byte pr_codes_SetColor[] = { 2, C_ESC, 'r' };
const byte pr_codes_SetYellow[] = { 3, C_ESC, 'r', 4 };
const byte pr_codes_SetCyan[] = { 3, C_ESC, 'r', 2 };
const byte pr_codes_SetMagenta[] = { 3, C_ESC, 'r', 1 };
const byte pr_codes_SetBlack[] = { 3, C_ESC, 'r', 0 };

/* the driver table: an extended driver, its devices in printDevices
 * (Port32/ABI.md 7.6) */
DriverExtendedInfoStruct DriverTable = {
    { DRIVER_STRATEGY(PrintStrategy), DA_HAS_EXTENDED_INFO,
      DRIVER_TYPE_PRINTER },
    0
};
