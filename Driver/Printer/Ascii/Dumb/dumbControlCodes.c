/*
 * dumbControlCodes.c -- the control codes of the Dumb ASCII driver (the C
 * port of dumbControlCodes.asm): a count byte, then the bytes; and the
 * driver table.
 */
#include "printcom.h"
#include "printcomDriver.h"

const byte pr_codes_ResetPrinter[] = { C_NULL };
const byte pr_codes_InitPrinter[] = { C_NULL };
const byte pr_codes_InitTextMode[] = { C_NULL };
const byte pr_codes_InitPaperLength[] = { C_NULL };
const byte pr_codes_ASFControl[] = { C_NULL };
const byte pr_codes_EnableASF[] = { C_NULL };
const byte pr_codes_DisableASF[] = { C_NULL };
const byte pr_codes_DefeatPaperOut[] = { C_NULL };
const byte pr_codes_Set10PitchRoman[] = { C_NULL };

/* the driver table: an extended driver, its devices in printDevices
 * (Port32/ABI.md 7.6) */
DriverExtendedInfoStruct DriverTable = {
    { DRIVER_STRATEGY(PrintStrategy), DA_HAS_EXTENDED_INFO,
      DRIVER_TYPE_PRINTER },
    0
};
