/*
 * toshiba24ControlCodes.c -- the control codes of the Toshiba 24-pin driver
 * (the C port of toshiba24ControlCodes.asm): a count byte, then the bytes;
 * and the driver table.
 */
#include "printcom.h"
#include "printcomDriver.h"

const byte pr_codes_ResetPrinter[] = { 3, C_ESC, C_CTRL_Z, 'I' };
const byte pr_codes_InitPrinter[] = {
    6,  /* byte count */
    C_ESC, 'Z',  /* no auto CR-LF */
    C_ESC, '.',  /* no auto LF */
    C_ESC, '>' };  /* unidirectional */
const byte pr_codes_InitTextMode[] = {
    2,  /* count */
    C_ESC, '<' };  /* set bi-directional */
const byte pr_codes_DefeatPaperOut[] = { C_NULL };
const byte pr_codes_InitPaperLength[] = { C_NULL };
const byte pr_codes_ASFControl[] = { C_NULL };
const byte pr_codes_EnableASF[] = { C_NULL };
const byte pr_codes_DisableASF[] = { C_NULL };
const byte pr_codes_AbsTab[] = { 2, C_ESC, C_HT };
const byte pr_codes_SetMaxLineFeed[] = { 4, C_ESC, 'L', '9', '9' };
const byte pr_codes_SetLineFeed[] = { 2, C_ESC, 'L' };
const byte pr_codes_GraphFeed[] = { 5, C_ESC, 'L', '0', '7', C_LF };
const byte pr_codes_SetMedGraphics[] = { 3, C_CR, C_ESC, ';' };
const byte pr_codes_SetHiGraphics[] = { 3, C_CR, C_ESC, 29 };
const byte pr_codes_Set10PitchDraft[] = { 6, C_ESC, '*', '0', C_ESC, 31,
    C_CR };
const byte pr_codes_Set12PitchDraft[] = { 6, C_ESC, '*', '0', C_ESC, 31,
    C_VT };
const byte pr_codes_Set15PitchDraft[] = { 6, C_ESC, '*', '0', C_ESC, 31,
    C_HT };
const byte pr_codes_Set10PitchRoman[] = { 6, C_ESC, '*', '2', C_ESC, 31,
    C_CR };
const byte pr_codes_Set12PitchRoman[] = { 6, C_ESC, '*', '2', C_ESC, 31,
    C_VT };
const byte pr_codes_Set15PitchRoman[] = { 6, C_ESC, '*', '2', C_ESC, 31,
    C_HT };
const byte pr_codes_Set17PitchRoman[] = { 6, C_ESC, '*', '2', C_ESC, 31,
    C_BS };
const byte pr_codes_SetProportionalRoman[] = { 3, C_ESC, '*', '3' };
const byte pr_codes_SetCondensed[] = { 2, C_ESC, '[' };
const byte pr_codes_SetNLQ[] = { 3, C_ESC, '*', '2' };
const byte pr_codes_SetBold[] = { 3, C_ESC, 'K', '3' };
const byte pr_codes_SetItalic[] = { 2, C_ESC, C_DC2 };
const byte pr_codes_SetUnderline[] = { 2, C_ESC, 'I' };
const byte pr_codes_SetDblWidth[] = { 2, C_ESC, '!' };
const byte pr_codes_SetShadow[] = { 2, C_ESC, 'Q' };
const byte pr_codes_ResetCondensed[] = { 2, C_ESC, ']' };
const byte pr_codes_ResetNLQ[] = { C_NULL };
const byte pr_codes_ResetBold[] = { 2, C_ESC, 'M' };
const byte pr_codes_ResetItalic[] = { 2, C_ESC, C_DC4 };
const byte pr_codes_ResetUnderline[] = { 2, C_ESC, 'J' };
const byte pr_codes_ResetDblWidth[] = { 2, C_ESC, '"' };
const byte pr_codes_ResetShadow[] = { 2, C_ESC, 'R' };

/* the driver table: an extended driver, its devices in printDevices
 * (Port32/ABI.md 7.6) */
DriverExtendedInfoStruct DriverTable = {
    { DRIVER_STRATEGY(PrintStrategy), DA_HAS_EXTENDED_INFO,
      DRIVER_TYPE_PRINTER },
    0
};
