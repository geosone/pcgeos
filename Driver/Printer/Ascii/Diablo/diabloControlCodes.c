/*
 * diabloControlCodes.c -- the control codes of the Diablo daisy wheel
 * driver (the C port of diabloControlCodes.asm): a count byte, then the
 * bytes; and the driver table.
 */
#include "printcom.h"
#include "printcomDriver.h"

const byte pr_codes_ResetPrinter[] = { 3, C_ESC, C_CTRL_Z, 'I' };
const byte pr_codes_InitPrinter[] = {
    2,  /* byte count */
    C_ESC, '/' };  /* bidirectional */
const byte pr_codes_InitTextMode[] = {
    2,  /* count */
    C_ESC, '/' };  /* set bi-directional */
const byte pr_codes_DefeatPaperOut[] = { C_NULL };
const byte pr_codes_InitPaperLength[] = { C_NULL };
const byte pr_codes_ASFControl[] = { C_NULL };
const byte pr_codes_EnableASF[] = { C_NULL };
const byte pr_codes_DisableASF[] = { C_NULL };
const byte pr_codes_AbsTab[] = { 2, C_ESC, C_HT };
const byte pr_codes_Set10PitchRoman[] = { 5, C_ESC, 'Q', C_ESC, 31, C_CR };
const byte pr_codes_Set12PitchRoman[] = { 5, C_ESC, 'Q', C_ESC, 31, C_VT };
const byte pr_codes_Set15PitchRoman[] = { 5, C_ESC, 'Q', C_ESC, 31, C_HT };
const byte pr_codes_Set17PitchRoman[] = { 5, C_ESC, 'Q', C_ESC, 31, C_BS };
const byte pr_codes_SetProportionalRoman[] = { 2, C_ESC, 'P' };
const byte pr_codes_SetBold[] = { 2, C_ESC, 'O' };
const byte pr_codes_SetItalic[] = {
    2, C_ESC, 'A' };  /* really set secondary color */
const byte pr_codes_SetUnderline[] = { 2, C_ESC, 'E' };
const byte pr_codes_ResetBold[] = { 2, C_ESC, '&' };
const byte pr_codes_ResetItalic[] = { 2, C_ESC, 'B' };
const byte pr_codes_ResetUnderline[] = { 2, C_ESC, 'R' };

/* the driver table: an extended driver, its devices in printDevices
 * (Port32/ABI.md 7.6) */
DriverExtendedInfoStruct DriverTable = {
    { DRIVER_STRATEGY(PrintStrategy), DA_HAS_EXTENDED_INFO,
      DRIVER_TYPE_PRINTER },
    0
};
