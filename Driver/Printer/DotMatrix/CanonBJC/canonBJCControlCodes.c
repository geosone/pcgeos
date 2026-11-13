/*
 * canonBJCControlCodes.c -- the control codes of the Canon BJC driver (the
 * C port of canonBJCControlCodes.asm): a count byte, then the bytes; and
 * the driver table.
 */
#include "printcom.h"
#include "printcomDriver.h"

const byte pr_codes_ResetPrinter[] = {
    7,  /* byte count */
    C_ESC, '[', 'K', 2, 0,  /* command = RESET, byte count */
    0, 15 };  /* init = 0, id = 15 */
const byte pr_codes_InitPrinter[] = {
    14,  /* byte count */
    C_ESC, '(', 'b', 1, 0,  /* command = COMPRESSION MODE, byte count */
    1,  /* compression = PackBits */
    C_ESC, '(', 't', 3, 0,  /* command = SET IMAGE FORMAT, byte count */
    1, 0, 0 };  /* bitdepth = 1, format = CMYK, ink = regular */
const byte pr_codes_SetPrintResolution[] = {
    5,
    C_ESC, '(', 'd', 4, 0 };  /* command = SET IMAGE RESOLUTION, byte count */
const byte pr_codes_SetPrintingMethod[] = {
    6,
    C_ESC, '(', 'c', 2, 0,  /* command = SET PRINT METHOD, byte count */
    16 };  /* color print */
const byte pr_codes_ReturnToEmulationMode[] = {
    2,  /* byte count */
    C_ESC, '@' };
const byte pr_codes_DoLineFeed[] = {
    5,  /* byte count */
    C_ESC, '(', 'e', 2, 0 };  /* command = RASTER SKIP, byte count */
const byte pr_codes_SetGraphics[] = {
    3,  /* byte count */
    C_ESC, '(', 'A' };  /* command = SET COLOR COMPONENT */
const byte canonBJColorTable[] = { 'Y', 'C', 'M', 'K' };

/* the driver table: an extended driver, its devices in printDevices
 * (Port32/ABI.md 7.6) */
DriverExtendedInfoStruct DriverTable = {
    { DRIVER_STRATEGY(PrintStrategy), DA_HAS_EXTENDED_INFO,
      DRIVER_TYPE_PRINTER },
    0
};
