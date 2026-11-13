/*
 * propx24ControlCodes.c -- the control codes of the IBM Proprinter X24
 * 24-pin driver (the C port of propx24ControlCodes.asm): a count byte, then
 * the bytes; and the driver table.
 */
#include "printcom.h"
#include "printcomDriver.h"

const byte pr_codes_ResetPrinter[] = { 1, C_CR };
const byte pr_codes_InitPrinter[] = {
    12,  /* count */
    C_ESC, '[', 92, 4, 0, 0, 0, 0, 180,  /* set vertical units to 180 */
    C_ESC, 'U', 1 };  /* set uni-directional */
const byte pr_codes_InitTextMode[] = {
    5,  /* count */
    C_ESC, 'U', 0,  /* set bi-directional */
    C_ESC, '6' };  /* use character set 2 */
const byte pr_codes_SetASCII7[] = { C_NULL };
const byte pr_codes_SetIBM437[] = {
    9,  /* count */
    C_ESC, '[', 'T', 4, 0, 0, 0, 1, 181 };
const byte pr_codes_SetIBM850[] = {
    9,  /* count */
    C_ESC, '[', 'T', 4, 0, 0, 0, 3, 82 };
const byte pr_codes_SetIBM860[] = {
    9,  /* count */
    C_ESC, '[', 'T', 4, 0, 0, 0, 3, 92 };
const byte pr_codes_SetIBM863[] = {
    9,  /* count */
    C_ESC, '[', 'T', 4, 0, 0, 0, 3, 95 };
const byte pr_codes_SetIBM865[] = {
    9,  /* count */
    C_ESC, '[', 'T', 4, 0, 0, 0, 3, 97 };
const byte pr_codes_InitPaperLength[] = {
    4, C_ESC, 'C', 0, 22 };  /* set page length to max (22") */
const byte pr_codes_FormFeed[] = {
    5,
    C_ESC, '3', 3,  /* set 3/180" line spacing */
    C_ESC, 'C' };  /* set the page length */
const byte pr_codes_SetTab[] = {
    2,
    C_ESC, 'D' };  /* set the tab */
const byte pr_codes_DoTab[] = { 2, 0, C_HT };
const byte pr_codes_DoLineFeed[] = { 2, C_ESC, 'J' };
const byte pr_codes_DoMaxLineFeed[] = { 3, C_ESC, 'J', PR_MAX_LINE_FEED };
const byte pr_codes_SetLoGraphics[] = { 0 };
const byte pr_codes_SetMedGraphics[] = { 11 };
const byte pr_codes_SetHiGraphics[] = { 12 };
const byte pr_codes_SetGraphics[] = { 4, C_CR, C_ESC, '[', 'g' };
const byte pr_codes_Set10PitchCourierDraft[] = { 3, C_ESC, 'I', 0 };
const byte pr_codes_Set12PitchCourierDraft[] = { 3, C_ESC, 'I', 8 };
const byte pr_codes_Set17PitchCourierDraft[] = { 3, C_ESC, 'I', 16 };
const byte pr_codes_Set10PitchCourierNLQ[] = { 3, C_ESC, 'I', 2 };
const byte pr_codes_Set12PitchCourierNLQ[] = { 5, C_ESC, ':', C_ESC, 'I',
    10 };
const byte pr_codes_Set17PitchCourierNLQ[] = { 3, C_ESC, 'I', 18 };
const byte pr_codes_SetProportionalRomanNLQ[] = { 3, C_ESC, 'I', 3 };
const byte pr_codes_Set10PitchRoman[] = {
    10, C_ESC, '[', 'I', 5, 0, 0, 11, 0, 144, 1 };  /* courier 10 pitch */
const byte pr_codes_Set12PitchRoman[] = {
    10, C_ESC, '[', 'I', 5, 0, 0, 85, 0, 120, 1 };  /* courier 12 pitch */
const byte pr_codes_Set15PitchRoman[] = {
    10, C_ESC, '[', 'I', 5, 0, 0, 223, 0, 96, 1 };  /* courier 15 pitch */
const byte pr_codes_Set17PitchRoman[] = {
    10, C_ESC, '[', 'I', 5, 0, 0, 254, 0, 84, 1 };  /* courier 17 pitch */
const byte pr_codes_Set20PitchRoman[] = {
    10, C_ESC, '[', 'I', 5, 0, 1, 238, 0, 72, 1 };  /* courier 20 pitch */
const byte pr_codes_Set24PitchRoman[] = {
    10, C_ESC, '[', 'I', 5, 0, 1, 30, 0, 60, 1 };  /* courier 24 pitch */
const byte pr_codes_SetProportionalRoman[] = {
    10, C_ESC, '[', 'I', 5, 0, 0, 171, 0, 0, 2 };  /* courier proportional */
const byte pr_codes_Set10PitchSans[] = {
    10, C_ESC, '[', 'I', 5, 0, 0, 36, 0, 144, 1 };  /* gothic 10 pitch */
const byte pr_codes_Set12PitchSans[] = {
    10, C_ESC, '[', 'I', 5, 0, 0, 87, 0, 120, 1 };  /* gothic 12 pitch */
const byte pr_codes_Set15PitchSans[] = {
    10, C_ESC, '[', 'I', 5, 0, 0, 222, 0, 96, 1 };  /* gothic 15 pitch */
const byte pr_codes_Set17PitchSans[] = {
    10, C_ESC, '[', 'I', 5, 0, 0, 255, 0, 84, 1 };  /* gothic 17 pitch */
const byte pr_codes_Set20PitchSans[] = {
    10, C_ESC, '[', 'I', 5, 0, 1, 140, 0, 72, 1 };  /* gothic 20 pitch */
const byte pr_codes_Set24PitchSans[] = {
    10, C_ESC, '[', 'I', 5, 0, 1, 32, 0, 60, 1 };  /* gothic 24 pitch */
const byte pr_codes_SetProportionalSans[] = {
    10, C_ESC, '[', 'I', 5, 0, 0, 174, 0, 0, 2 };  /* gothic proportional */
const byte pr_codes_SetCondensed[] = { 1, C_SI };
const byte pr_codes_SetSubscript[] = { 3, C_ESC, 'S', 1 };
const byte pr_codes_SetSuperscript[] = { 3, C_ESC, 'S', 0 };
const byte pr_codes_SetNLQ[] = { 3, C_ESC, 'x', 1 };
const byte pr_codes_SetBold[] = { 4, C_ESC, 'E', C_ESC, 'G' };
const byte pr_codes_SetItalic[] = { 6, C_ESC, '[', '@', 1, 0, 1 };
const byte pr_codes_SetUnderline[] = { 3, C_ESC, '-', 1 };
const byte pr_codes_SetDblWidth[] = { 3, C_ESC, 'W', 1 };
const byte pr_codes_SetDblHeight[] = { 8, C_ESC, '[', '@', 3, 0, 0, 0, 2 };
const byte pr_codes_SetOverline[] = { 1, C_NULL };
const byte pr_codes_ResetCondensed[] = { 1, C_DC2 };
const byte pr_codes_ResetScript[] = { 2, C_ESC, 'T' };
const byte pr_codes_ResetNLQ[] = { 3, C_ESC, 'x', 0 };
const byte pr_codes_ResetBold[] = { 4, C_ESC, 'F', C_ESC, 'H' };
const byte pr_codes_ResetItalic[] = { 6, C_ESC, '[', '@', 1, 0, 2 };
const byte pr_codes_ResetUnderline[] = { 3, C_ESC, '-', 0 };
const byte pr_codes_ResetDblWidth[] = { 3, C_ESC, 'W', 0 };
const byte pr_codes_ResetDblHeight[] = { 8, C_ESC, '[', '@', 3, 0, 0, 0, 1 };
const byte pr_codes_ResetOverline[] = { 1, C_NULL };
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
