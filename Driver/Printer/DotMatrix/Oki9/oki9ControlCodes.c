/*
 * oki9ControlCodes.c -- the control codes of the Okidata 9-pin driver (the
 * C port of oki9ControlCodes.asm): a count byte, then the bytes; and the
 * driver table.
 */
#include "printcom.h"
#include "printcomDriver.h"

const byte pr_codes_ResetPrinter[] = {
    1,
    C_DC1 };  /* enable printing */
const byte pr_codes_InitPrinter[] = {
    15,  /* byte count */
    C_ESC, '%', 'C', '0', '0', '1',  /* set margin all the way to left */
    30,  /* set 10-pitch */
    C_ESC, 75,  /* reset superscript */
    C_ESC, 77,  /* reset subscript */
    C_ESC, 'I',  /* reset bold */
    C_ESC, 'D' };  /* reset underline */
const byte pr_codes_InitTextMode[] = {
    C_NULL };  /* count */
const byte pr_codes_InitPaperLength[] = {
    8,
    C_ESC, '%', '9', PR_MAX_LINE_FEED,  /* set line spacing to max */
    C_ESC, 'F', '9', '9' };  /* set the form length to 99 line */
const byte pr_codes_FormFeed[] = {
    6,
    C_ESC, '%', '9', 2,  /* set 2/144" line spacing */
    C_ESC, 'F' };  /* set the page length */
const byte pr_codes_SetTab[] = { 2, C_ESC, C_HT };
const byte pr_codes_DoTab[] = { 2, C_CR, C_HT };
const byte pr_codes_SetLineFeed[] = { 3, C_ESC, '%', '9' };
const byte pr_codes_SetMaxLineFeed[] = { 4, C_ESC, '%', '9',
    PR_MAX_LINE_FEED };
const byte pr_codes_Do1ScanlineFeed[] = { 5, C_ESC, '%', '9', 1, C_LF };
const byte pr_codes_EnterGraphics[] = { 3, C_CR, 28, 3 };
const byte pr_codes_ExitGraphics[] = { 2, 3, 2 };
const byte pr_codes_Set5Pitch[] = { 2, 30, 31 };
const byte pr_codes_Set6Pitch[] = { 2, 28, 31 };
const byte pr_codes_Set10Pitch[] = { 1, 30 };
const byte pr_codes_Set12Pitch[] = { 1, 28 };
const byte pr_codes_Set17Pitch[] = { 1, 29 };
const byte pr_codes_SetProportional[] = {
    C_NULL };  /* no proport. code, but code has to be present */
const byte pr_codes_SetSubscript[] = { 2, C_ESC, 'L' };
const byte pr_codes_SetSuperscript[] = { 2, C_ESC, 'J' };
const byte pr_codes_SetNLQ[] = { 2, C_ESC, '1' };
const byte pr_codes_SetBold[] = { 2, C_ESC, 'H' };
const byte pr_codes_SetItalic[] = { 2, C_ESC, 'T' };
const byte pr_codes_SetUnderline[] = { 2, C_ESC, 'C' };
const byte pr_codes_ResetSuperscript[] = { 2, C_ESC, 75 };
const byte pr_codes_ResetSubscript[] = { 2, C_ESC, 77 };
const byte pr_codes_ResetNLQ[] = { 2, C_ESC, '0' };
const byte pr_codes_ResetBold[] = { 2, C_ESC, 'I' };
const byte pr_codes_ResetItalic[] = { 2, C_ESC, 'I' };
const byte pr_codes_ResetUnderline[] = { 2, C_ESC, 'D' };

/* the driver table: an extended driver, its devices in printDevices
 * (Port32/ABI.md 7.6) */
DriverExtendedInfoStruct DriverTable = {
    { DRIVER_STRATEGY(PrintStrategy), DA_HAS_EXTENDED_INFO,
      DRIVER_TYPE_PRINTER },
    0
};
