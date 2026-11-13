/*
 * lbpControlCodes.c -- the control codes of the Canon CaPSL driver (the C
 * port of lbpControlCodes.asm): a count byte, then the bytes; and the
 * driver table.
 */
#include "printcom.h"
#include "printcomDriver.h"

const byte pr_codes_CSIcode[] = { 2, C_ESC, '[' };
const byte pr_codes_ResetPrinter[] = {
    4,
    C_ESC, ';',  /* enter ISO Mode */
    C_ESC, '<' };  /* Soft Reset */
const byte pr_codes_InitPaperLetter[] = {
    6,
    C_ESC, '[', '3', '0', ';', ';' };  /* portrait letter paper size */
const byte pr_codes_InitPaperLegal[] = {
    6,
    C_ESC, '[', '3', '2', ';', ';' };  /* portrait legal paper size */
const byte pr_codes_InitPaperA4[] = {
    6,
    C_ESC, '[', '1', '4', ';', ';' };  /* portrait A4 paper size */
const byte pr_codes_InitPaperB5[] = {
    6,
    C_ESC, '[', '2', '6', ';', ';' };  /* portrait B5 paper size */
const byte pr_codes_InitPaperCustom[] = {
    5,
    C_ESC, '[', '8', '0', ';' };  /* custom paper size */
const byte pr_codes_InitPrinter[] = {
    48,
    'p',  /* FINISH SETTING paper size, and format */
    C_ESC, '[', '3', '&', 'z',  /* enter extended full paint mode */
    C_ESC, '[', '?', '2', 'h',  /* disable auto FF */
    C_ESC, '[', '?', '1', 'l',  /* disable auto NL */
    C_ESC, '[', '7', ' ', 'I',  /* Set size unit to 1/300" */
    C_ESC, '[', '1', '1', 'h',  /* set positional units to SIZE */
    C_ESC, '[', '0', ';', '0', 'f',  /* set CAP to 0,0 */
    C_ESC, '(', 39, 36, 50,  /* set primary graphic char set to IBML */
    C_ESC, ')', 39, 32, 49,  /* set secondary graphic char set IBMR1 */
    C_ESC, '[', '?', '3', '2', 'h' };  /* enable scalable font selection */
const byte pr_codes_SetGraphics[] = {
    5,
    ';', '7', '5', '.', 'r', 0, 0,  /* Set Graphics resolution to Low Res */
    6,
    ';', '1', '5', '0', '.', 'r', 0,  /* Set Graphics resolution to Med Res */
    6,
    /* Set Graphics resolution to Hi Res */
    ';', '3', '0', '0', '.', 'r', 0 };
const byte pr_codes_SetDutch[] = { 6, C_ESC, '[', '5', ';', '5', 'y' };
const byte pr_codes_SetSwiss[] = { 6, C_ESC, '[', '4', ';', '4', 'y' };
const byte pr_codes_SetCourier[] = { 6, C_ESC, '[', '3', ';', '3', 'y' };
const byte pr_codes_SetPitchStart[] = { 3, C_ESC, '[', '?' };
const byte pr_codes_SetPitchEnd[] = { 3, '0', ' ', 'K' };
const byte pr_codes_SetSizeStart[] = { 2, C_ESC, '[' };
const byte pr_codes_SetSizeEnd[] = { 2, ' ', 'C' };
const byte pr_codes_Set10Pitch[] = { 5, C_ESC, '[', '0', ' ', 'K' };
const byte pr_codes_Set12Pitch[] = { 5, C_ESC, '[', '1', ' ', 'K' };
const byte pr_codes_Set15Pitch[] = { 5, C_ESC, '[', '2', ' ', 'K' };
const byte pr_codes_SetProportional[] = { 6, C_ESC, '[', '?', '0', ' ', 'K' };
const byte pr_codes_SetBold[] = { 4, C_ESC, '[', '1', 'm' };
const byte pr_codes_SetItalic[] = { 4, C_ESC, '[', '3', 'm' };
const byte pr_codes_SetUnderline[] = { 4, C_ESC, '[', '4', 'm' };
const byte pr_codes_SetShadow[] = { 5, C_ESC, '[', '?', '6', 'm' };
const byte pr_codes_SetOutline[] = { 5, C_ESC, '[', '?', '7', 'm' };
const byte pr_codes_SetReverse[] = { 4, C_ESC, '[', '7', 'm' };
const byte pr_codes_SetDblWidth[] = { 11, C_ESC, '[', '1', '0', '0', ';', '2',
    '0', '0', ' ', 'B' };
const byte pr_codes_ResetBold[] = { 5, C_ESC, '[', '2', '2', 'm' };
const byte pr_codes_ResetItalic[] = { 5, C_ESC, '[', '2', '3', 'm' };
const byte pr_codes_ResetUnderline[] = { 5, C_ESC, '[', '2', '4', 'm' };
const byte pr_codes_ResetShadow[] = { 6, C_ESC, '[', '?', '2', '6', 'm' };
const byte pr_codes_ResetOutline[] = { 6, C_ESC, '[', '?', '2', '7', 'm' };
const byte pr_codes_ResetReverse[] = { 5, C_ESC, '[', '2', '7', 'm' };
const byte pr_codes_ResetDblWidth[] = { 11, C_ESC, '[', '1', '0', '0', ';',
    '1', '0', '0', ' ', 'B' };

/* the driver table: an extended driver, its devices in printDevices
 * (Port32/ABI.md 7.6) */
DriverExtendedInfoStruct DriverTable = {
    { DRIVER_STRATEGY(PrintStrategy), DA_HAS_EXTENDED_INFO,
      DRIVER_TYPE_PRINTER },
    0
};
