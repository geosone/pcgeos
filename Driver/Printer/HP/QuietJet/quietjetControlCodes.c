/*
 * quietjetControlCodes.c -- the control codes of the HP QuietJet driver
 * (the C port of quietjetControlCodes.asm): a count byte, then the bytes;
 * and the driver table.
 */
#include "printcom.h"
#include "printcomDriver.h"

const byte pr_codes_PJLEntryAndResetNoNeg[] = { 2, C_ESC, 'E' };
const byte pr_codes_PJLExit[] = { 2, C_ESC, 'E' };
const byte pr_codes_ResetPrinter[] = { 2, C_ESC, 'E' };
const byte pr_codes_CursorPosition[] = { 3, C_ESC, '*', 'p' };
const byte pr_codes_Do1ScanlineFeed[] = { 6, C_ESC, '*', 'b', '1', 'W', 0 };
const byte pr_codes_StartGraphicLine[] = { 8, C_ESC, '*', 'r', '1', 'A',
    C_ESC, '*', 'b' };
const byte pr_codes_EndGraphics[] = { 4, C_ESC, '*', 'r', 'B' };
const word pr_graphic_Res_Values[3] = { 96, 0, 192 };
const byte pr_codes_SetASCII7[] = { 4, C_ESC, '(', '0', 'U' };
const byte pr_codes_SetIBM437[] = { 5, C_ESC, '(', '1', '0', 'U' };
const byte pr_codes_SetRoman8[] = { 4, C_ESC, '(', '8', 'U' };
const byte pr_codes_SetPageLength[] = {
    5,
    C_ESC, '&', 'l', '8', 'd' };  /* 8lpi */
const byte pr_codes_MidPageLength[] = {
    3,
    'p', '2', 'e' };  /* 2 line top margin */
const byte pr_codes_FinishPageLength[] = {
    6,
    'F', C_ESC, '&', 'a', '0', 'R' };  /* set to top margin */
const byte pr_codes_InitPrinter[] = { 5, C_ESC, '&', 'l', '0', 'L' };
const byte pr_codes_Set5PitchRoman[] = { 7, C_ESC, '(', 's', '0', 'p', '5',
    'H' };
const byte pr_codes_Set6PitchRoman[] = { 7, C_ESC, '(', 's', '0', 'p', '6',
    'H' };
const byte pr_codes_Set10PitchRoman[] = { 8, C_ESC, '(', 's', '0', 'p', '1',
    '0', 'H' };
const byte pr_codes_Set106PitchRoman[] = { 10, C_ESC, '(', 's', '0', 'p', '1',
    '0', '.', '6', 'H' };
const byte pr_codes_Set12PitchRoman[] = { 8, C_ESC, '(', 's', '0', 'p', '1',
    '2', 'H' };
const byte pr_codes_Set213PitchRoman[] = { 10, C_ESC, '(', 's', '0', 'p', '2',
    '1', '.', '3', 'H' };
const byte pr_codes_SetProportionalRoman[] = { 5, C_ESC, '(', 's', '1', 'P' };
const byte pr_codes_SetNLQ[] = { 5, C_ESC, '(', 's', '1', 'Q' };
const byte pr_codes_SetBold[] = { 5, C_ESC, '(', 's', '1', 'B' };
const byte pr_codes_SetUnderline[] = { 4, C_ESC, '&', 'd', 'D' };
const byte pr_codes_ResetNLQ[] = { 5, C_ESC, '(', 's', '0', 'Q' };
const byte pr_codes_ResetBold[] = { 5, C_ESC, '(', 's', '0', 'B' };
const byte pr_codes_ResetUnderline[] = { 4, C_ESC, '&', 'd', '@' };
const byte pr_codes_SetGraphicRes[] = {
    /* # = resolution in dpi */
    C_ESC, '*', 't', '#', 'R', C_ESC, '*', 'r', '1', 'A', 0 };
const byte pr_codes_TransferGraphics[] = { C_ESC, '*', 'b', '#', 'W', 0 };
const byte pr_codes_SetPitch[] = {
    C_ESC, '(', 's', '0', 'p', '#', 'H', 0 };  /* # = Font Pitch */
const byte pr_codes_DuplexMode[] = { C_ESC, '&', 'l', '#', 'S', 0 };
const byte pr_codes_SetInputPath[] = { C_ESC, '&', 'l', '#', 'H', 0 };

/* the driver table: an extended driver, its devices in printDevices
 * (Port32/ABI.md 7.6) */
DriverExtendedInfoStruct DriverTable = {
    { DRIVER_STRATEGY(PrintStrategy), DA_HAS_EXTENDED_INFO,
      DRIVER_TYPE_PRINTER },
    0
};
