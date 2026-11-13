/*
 * star9ControlCodes.c -- the control codes of the Star 9-pin driver (the C
 * port of star9ControlCodes.asm): a count byte, then the bytes; and the
 * driver table.
 */
#include "printcom.h"
#include "printcomDriver.h"

const byte pr_codes_ResetPrinter[] = { 2, C_ESC, '@' };
const byte pr_codes_InitPrinter[] = {
    5,
    C_ESC, 'O',  /* disable perf skip */
    C_ESC, 'U', 1 };  /* unidirectional print */
const byte pr_codes_InitTextMode[] = {
    3,  /* count */
    C_ESC, 'U', 0 };  /* set bi-directional */
const byte pr_codes_DefeatPaperOut[] = { 2, C_ESC, '8' };
const byte pr_codes_SetCountry[] = {
    2,  /* count */
    C_ESC, '7' };
const byte pr_codes_InitPaperLength[] = {
    4, C_ESC, 'C', 0, 22 };  /* set page length to max (22") */
const byte pr_codes_FormFeed[] = {
    5,
    C_ESC, '3', 2,  /* set 2/144" line spacing */
    C_ESC, 'C' };  /* set the page length */
const byte pr_codes_ASFControl[] = { C_NULL };
const byte pr_codes_EnableASF[] = { C_NULL };
const byte pr_codes_DisableASF[] = { C_NULL };
const byte pr_codes_SetTab[] = { 2, C_ESC, 'D' };
const byte pr_codes_DoTab[] = { 3, 0, C_SPACE, C_HT };
const byte pr_codes_DoLineFeed[] = { 2, C_ESC, 'J' };
const byte pr_codes_DoMaxLineFeed[] = { 3, C_ESC, 'J', PR_MAX_LINE_FEED };
const byte pr_codes_Do1ScanlineFeed[] = { 3, C_ESC, 'J', 1 };
const byte pr_codes_SetLoGraphics[] = { 3, C_CR, C_ESC, 'K' };
const byte pr_codes_SetMedGraphics[] = { 3, C_CR, C_ESC, 'L' };
const byte pr_codes_SetHiGraphics[] = { 3, C_CR, C_ESC, 'z' };
const byte pr_codes_Set10Pitch[] = { 6, C_ESC, 'p', 0, C_ESC, 'B', 1 };
const byte pr_codes_Set12Pitch[] = { 6, C_ESC, 'p', 0, C_ESC, 'B', 2 };
const byte pr_codes_Set17Pitch[] = { 6, C_ESC, 'p', 0, C_ESC, 'B', 3 };
const byte pr_codes_SetProportional[] = { 3, C_ESC, 'p', 1 };
const byte pr_codes_SetSubscript[] = { 3, C_ESC, 'S', 1 };
const byte pr_codes_SetSuperscript[] = { 3, C_ESC, 'S', 0 };
const byte pr_codes_SetNLQ[] = { 3, C_ESC, 'B', 4 };
const byte pr_codes_SetBold[] = { 4, C_ESC, 'E', C_ESC, 'G' };
const byte pr_codes_SetItalic[] = { 2, C_ESC, '4' };
const byte pr_codes_SetUnderline[] = { 3, C_ESC, '-', 1 };
const byte pr_codes_SetDblWidth[] = { 3, C_ESC, 'W', 1 };
const byte pr_codes_ResetScript[] = { 2, C_ESC, 'T' };
const byte pr_codes_ResetNLQ[] = { 3, C_ESC, 'B', 5 };
const byte pr_codes_ResetBold[] = { 4, C_ESC, 'F', C_ESC, 'H' };
const byte pr_codes_ResetItalic[] = { 2, C_ESC, '5' };
const byte pr_codes_ResetUnderline[] = { 3, C_ESC, '-', 0 };
const byte pr_codes_ResetDblWidth[] = { 3, C_ESC, 'W', 0 };
const byte pr_codes_SetCondensed[] = { C_NULL };
const byte pr_codes_ResetCondensed[] = { C_NULL };
const byte pr_codes_SetDblHeight[] = { C_NULL };
const byte pr_codes_ResetDblHeight[] = { C_NULL };

/* the driver table: an extended driver, its devices in printDevices
 * (Port32/ABI.md 7.6) */
DriverExtendedInfoStruct DriverTable = {
    { DRIVER_STRATEGY(PrintStrategy), DA_HAS_EXTENDED_INFO,
      DRIVER_TYPE_PRINTER },
    0
};
