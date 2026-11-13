/*
 * pscriptTables.c -- the driver's tables (the C port of
 * pscriptTables.asm and the PrintDriverInfo of pscriptDriverInfo.asm;
 * the devices are generated, pscriptDevices.c).
 */
#include "pscript.h"

extern PrintRoutine PrintSetPageTransform, PrintGString;

/* the escapes: PDL printers take the page transform and gstrings */
const PrintEscEntry printEscTable[] = {
    { DR_PRINT_ESC_SET_PAGE_TRANSFORM, PrintSetPageTransform },
    { DR_PRINT_ESC_PRINT_GSTRING, PrintGString }
};
const word printNumEscapes = sizeof(printEscTable) / sizeof(printEscTable[0]);

/* PrintDriverInfo: the ISO and ASCII tables were chunks of DriverInfo;
 * this driver's were empty (0ffffh and ";;") */
const PrintDriverInfo printDriverInfo = {
    90,			/* timeout (sec) */
    PR_RESEND,
    0,			/* PDI_subISOTable */
    0,			/* PDI_asciiTransChars */
    PDT_PRINTER,
    FALSE		/* PDI_uiFeatures */
};

/* the driver table: an extended driver.  DEIS_resource is 0: the device
 * list is printDevices, not a DriverInfo resource (Port32/ABI.md 7.6). */
DriverExtendedInfoStruct DriverTable = {
    { DRIVER_STRATEGY(PrintStrategy), DA_HAS_EXTENDED_INFO,
      DRIVER_TYPE_PRINTER },
    0
};
