/*
 * citoh9ControlCodes.c -- the control codes of the C.Itoh 9-pin driver (the
 * C port of citoh9ControlCodes.asm): a count byte, then the bytes; and the
 * driver table.
 */
#include "printcom.h"
#include "printcomDriver.h"

const byte pr_codes_ResetPrinter[] = { 2, C_ESC, 'c' };
const byte pr_codes_InitPrinter[] = {
    2, C_ESC, '>' };  /* unidirectional print */
const byte pr_codes_InitTextMode[] = {
    2,  /* count */
    C_ESC, '<' };  /* set bi-directional */
const byte pr_codes_AbsPos[] = { 3, C_CR, C_ESC, 'F' };
const byte pr_codes_SetLineFeed[] = { 2, C_ESC, 'T' };
const byte pr_codes_SetMaxLineFeed[] = { 4, C_ESC, 'T', '9', '9' };
const byte pr_codes_Do1ScanlineFeed[] = { 5, C_ESC, 'T', '0', '1', C_LF };
const byte pr_codes_InitPaperLength[] = { C_NULL };
const byte pr_codes_ASFControl[] = { C_NULL };
const byte pr_codes_EnableASF[] = { C_NULL };
const byte pr_codes_DisableASF[] = { C_NULL };
const byte pr_codes_DefeatPaperOut[] = { C_NULL };
const byte pr_codes_SetLoGraphics[] = { 5, C_CR, C_ESC, 'N', C_ESC, 'S' };
const byte pr_codes_SetHiGraphics[] = { 5, C_CR, C_ESC, 'P', C_ESC, 'S' };
const byte pr_codes_Set10Pitch[] = { 2, C_ESC, 'N' };
const byte pr_codes_Set12Pitch[] = { 2, C_ESC, 'E' };
const byte pr_codes_SetProportional[] = { 2, C_ESC, 'P' };
const byte pr_codes_Set17Pitch[] = { 2, C_ESC, 'Q' };
const byte pr_codes_SetCondensed[] = { C_NULL };
const byte pr_codes_SetSubscript[] = { 3, C_ESC, 's', '2' };
const byte pr_codes_SetSuperscript[] = { 3, C_ESC, 's', '1' };
const byte pr_codes_SetNLQ[] = { 2, C_ESC, '!' };
const byte pr_codes_SetBold[] = { 2, C_ESC, '!' };
const byte pr_codes_SetItalic[] = { 3, C_ESC, 'i', '1' };
const byte pr_codes_SetUnderline[] = { 2, C_ESC, 'X' };
const byte pr_codes_SetDblWidth[] = { 1, C_SO };
const byte pr_codes_ResetCondensed[] = { C_NULL };
const byte pr_codes_ResetScript[] = { 3, C_ESC, 's', '0' };
const byte pr_codes_ResetNLQ[] = { 2, C_ESC, '"' };
const byte pr_codes_ResetBold[] = { 2, C_ESC, '"' };
const byte pr_codes_ResetItalic[] = { 3, C_ESC, 'i', '0' };
const byte pr_codes_ResetUnderline[] = { 2, C_ESC, 'Y' };
const byte pr_codes_ResetDblWidth[] = { 1, C_SI };
const byte pr_codes_SetStrikeThru[] = { C_NULL };
const byte pr_codes_SetDblHeight[] = { C_NULL };
const byte pr_codes_ResetStrikeThru[] = { C_NULL };
const byte pr_codes_ResetDblHeight[] = { C_NULL };
const byte pr_codes_SetColor[] = { 2, C_ESC, 'K' };
const byte pr_codes_SetYellow[] = { 3, C_ESC, 'K', '1' };
const byte pr_codes_SetCyan[] = { 3, C_ESC, 'K', '3' };
const byte pr_codes_SetMagenta[] = { 3, C_ESC, 'K', '2' };
const byte pr_codes_SetBlack[] = { 3, C_ESC, 'K', '0' };

/* the driver table: an extended driver, its devices in printDevices
 * (Port32/ABI.md 7.6) */
DriverExtendedInfoStruct DriverTable = {
    { DRIVER_STRATEGY(PrintStrategy), DA_HAS_EXTENDED_INFO,
      DRIVER_TYPE_PRINTER },
    0
};
