/*
 * dj500cControlCodes.c -- the control codes of the HP DeskJet 500C driver
 * (the C port of dj500cControlCodes.asm): a count byte, then the bytes; and
 * the driver table.
 */
#include "printcom.h"
#include "printcomDriver.h"

const byte pr_codes_ResetPrinter[] = { 2, C_ESC, 'E' };
const byte pr_codes_CursorPosition[] = { 3, C_ESC, '*', 'p' };
const byte pr_codes_TransferNoGraphics[] = { 7, C_ESC, '*', 'b', '0', 'm',
    '0', 'V' };
const byte pr_codes_Do1ScanlineFeed[] = { 7, C_ESC, '*', 'b', '0', 'm', '0',
    'W' };
const byte pr_codes_EndGraphics[] = { 5, C_ESC, '*', 'r', 'b', 'C' };
const byte pr_codes_SetPageLength[] = {
    5,
    C_ESC, '&', 'l', '8', 'd' };  /* 8lpi */
const byte pr_codes_MidPageLength[] = {
    3,
    'p', '2', 'e' };  /* 2 line top margin */
const byte pr_codes_FinishPageLength[] = {
    6,
    'F', C_ESC, '&', 'a', '0', 'R' };  /* set to top margin */
const byte pr_codes_InitPrinter[] = {
    18,
    C_ESC, '&', 'l', '0', 'o',  /* Force set Portrait orientation */
    '0', 'L',  /* defeat perf skip feature */
    C_ESC, '*', 'r', '-', '3', 'U',  /* use 3 plane CMY pallette */
    C_ESC, '*', 'p', '2', 'N' };  /* set unidirectional printing */
const byte pr_codes_PJLEntryAndResetNoNeg[] = {
    41,  /* count */
    C_ESC, '%', '-', '1', '2', '3', '4', '5', 'X',  /* UEL Code */
    '@', 'P', 'J', 'L', ' ', 'E', 'N', 'T', 'E', 'R', ' ', 'L', 'A', 'N', 'G', 'U', 'A', 'G', 'E', '=', 'P', 'C', 'L', C_CR, C_LF,
    C_ESC, 'E',  /* reset printer */
    C_ESC, '&', 'a', '1', 'N' };  /* set no negative motion */
const byte pr_codes_PJLExit[] = {
    15,  /* count */
    /* UEL Code */
    C_ESC, '%', '-', '1', '2', '3', '4', '5', 'X', '@', 'P', 'J', 'L', C_CR, C_LF };
const byte pr_codes_SetASCII7[] = { 4, C_ESC, '(', '0', 'U' };
const byte pr_codes_SetIBM437[] = { 5, C_ESC, '(', '1', '0', 'U' };
const byte pr_codes_SetIBM850[] = { 5, C_ESC, '(', '1', '2', 'U' };
const byte pr_codes_SetRoman8[] = { 4, C_ESC, '(', '8', 'U' };
const byte pr_codes_SetLatin1[] = { 4, C_ESC, '(', '0', 'N' };
const word pr_graphic_Res_Values[3] = { 75, 150, 300 };
const byte pr_codes_SetGraphicRes[] = {
    /* # = resolution in dpi */
    C_ESC, '*', 't', '#', 'R', C_ESC, '*', 'r', '1', 'A', 0 };
const byte pr_codes_TIFFTransferGraphics[] = { C_ESC, '*', 'b', '2', 'm', '#',
    'V', 0 };
const byte pr_codes_TransferGraphics[] = { C_ESC, '*', 'b', '0', 'm', '#',
    'V', 0 };
const byte pr_codes_DuplexMode[] = { C_ESC, '&', 'l', '#', 'S', 0 };
const byte pr_codes_SetInputPath[] = { C_ESC, '&', 'l', '#', 'H', 0 };

/* the driver table: an extended driver, its devices in printDevices
 * (Port32/ABI.md 7.6) */
DriverExtendedInfoStruct DriverTable = {
    { DRIVER_STRATEGY(PrintStrategy), DA_HAS_EXTENDED_INFO,
      DRIVER_TYPE_PRINTER },
    0
};
