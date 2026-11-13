/*
 * grpr9ControlCodes.c -- the control codes of the IBM Graphics 9-pin driver
 * (the C port of grpr9ControlCodes.asm): a count byte, then the bytes; and
 * the driver table.
 */
#include "printcom.h"
#include "printcomDriver.h"

const byte pr_codes_ResetPrinter[] = { 1, C_NULL };
const byte pr_codes_InitPrinter[] = {
    10, 0,  /* send a null to wait till after any reset garb */
    C_ESC, 'O',  /* cancel any perforation skipping */
    C_ESC, 'P', 0, C_DC2,  /* non proportional, 10 pitch */
    C_ESC, 'U', 1 };  /* set uni-directional */
const byte pr_codes_InitTextMode[] = {
    3,  /* count */
    C_ESC, 'U', 0 };  /* set bi-directional */
const byte pr_codes_InitPaperLength[] = {
    4, C_ESC, 'C', 0, 22 };  /* set page length to max (22") */
const byte pr_codes_FormFeed[] = {
    5,
    C_ESC, '3', 3,  /* set 3/216" line spacing */
    C_ESC, 'C' };  /* set the page length */
const byte pr_codes_SetTab[] = { 4, C_CR, C_SPACE, C_ESC, 'D' };
const byte pr_codes_DoTab[] = {
    3, 0, C_HT, C_SPACE };  /* space is for the column # starting */
const byte pr_codes_DoLineFeed[] = { 2, C_ESC, 'J' };
const byte pr_codes_DoMaxLineFeed[] = { 3, C_ESC, 'J', PR_MAX_LINE_FEED };
const byte pr_codes_Do1ScanlineFeed[] = { 3, C_ESC, 'J', 1 };
const byte pr_codes_SetLoGraphics[] = { 3, C_CR, C_ESC, 'K' };
const byte pr_codes_SetMedGraphics[] = { 3, C_CR, C_ESC, 'L' };
const byte pr_codes_SetHiGraphics[] = { 3, C_CR, C_ESC, 'Z' };
const byte pr_codes_Set10Pitch[] = { 4, C_ESC, 'P', 0, C_DC2 };
const byte pr_codes_Set12Pitch[] = { 5, C_ESC, 'P', 0, C_ESC, ':' };
const byte pr_codes_SetProportional[] = { 3, C_ESC, 'P', 1 };
const byte pr_codes_SetCondensed[] = { 1, C_SI };
const byte pr_codes_SetSubscript[] = { 3, C_ESC, 'S', 1 };
const byte pr_codes_SetSuperscript[] = { 3, C_ESC, 'S', 0 };
const byte pr_codes_SetNLQ[] = { 3, C_ESC, 'I', 2 };
const byte pr_codes_SetBold[] = { 4, C_ESC, 'E', C_ESC, 'G' };
const byte pr_codes_SetUnderline[] = { 3, C_ESC, '-', 1 };
const byte pr_codes_SetOverline[] = { 3, C_ESC, '_', 1 };
const byte pr_codes_SetDblWidth[] = { 3, C_ESC, 'W', 1 };
const byte pr_codes_ResetCondensed[] = { 1, C_DC2 };
const byte pr_codes_ResetScript[] = { 2, C_ESC, 'T' };
const byte pr_codes_ResetNLQ[] = { 3, C_ESC, 'I', 0 };
const byte pr_codes_ResetBold[] = { 4, C_ESC, 'F', C_ESC, 'H' };
const byte pr_codes_ResetUnderline[] = { 3, C_ESC, '-', 0 };
const byte pr_codes_ResetOverline[] = { 3, C_ESC, '_', 0 };
const byte pr_codes_ResetDblWidth[] = { 3, C_ESC, 'W', 0 };
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
