/*
 * libPrinter.c -- the printers the user installed (the C port of
 * libPrinter.asm), in the ini file:
 *
 *   [printer]
 *   printers = {name...}	the printers, a string section each
 *   count = n			all printers
 *   numPrinters = n		per PrinterDriverType: numPrinters,
 *				numPlotters, numFacsimiles, numCameras,
 *				numOthers
 *   defaultPrinter = n
 *
 * and a category per printer, its name, with "type" (the
 * PrinterDriverType) and what the printer driver and port need.
 * A change is told to the installed printers list
 * (MSG_PRINTER_INSTALLED_REMOVED).  Port32: the mailbox's medium
 * notifications (SpoolPrinterNameToMedium) are not sent: there is no
 * mailbox library.
 */
#include "spoolLocal.h"

static const char printerCat[] = "printer";
static const char printersKey[] = "printers";
static const char countKey[] = "count";
static const char defaultKey[] = "defaultPrinter";
static const char typeKey[] = "type";
static const char *const typeKeys[] = {
    countKey, "numPrinters", "numPlotters", "numFacsimiles", "numCameras",
    "numOthers"
};

/* GetTypeKeyString: the count key of a type; PDT_ALL (0xff): all */
static const char *GetTypeKeyString(PrinterDriverType type)
{
    word i = (byte) (type + 1);

    return i < sizeof(typeKeys) / sizeof(typeKeys[0]) ? typeKeys[i]
						      : countKey;
}

/* the count of this ini file (the first one with the key) */
static word ReadLocalCount(const char *key)
{
    word v;

    return InitFileReadInteger(printerCat, key, &v) ? 0 : v;
}

static Boolean CountCB(const char *section, word num, void *data)
{
    sdword v;

    (void) num;
    if (!UtilAsciiToHex32(section, &v))
	*(word *) data = (word) (*(word *) data + (word) v);
    return FALSE;
}

/* the counts of all ini files, added */
static word ReadAllCount(const char *key)
{
    word n = 0;

    InitFileEnumStringSection(printerCat, key, IFRF_READ_ALL, CountCB, &n);
    return n;
}

static word AddCount(const char *key, sword by)
{
    word n = (word) (ReadLocalCount(key) + by);

    InitFileWriteInteger(printerCat, key, n);
    return n;
}

word SpoolGetNumPrinters(PrinterDriverType driverType, byte localOnlyFlag)
{
    const char *key = GetTypeKeyString(driverType);

    return localOnlyFlag ? ReadLocalCount(key) : ReadAllCount(key);
}

PrinterDriverType ReadPrinterDriverType(const char *printer)
{
    word v;

    return InitFileReadInteger(printer, typeKey, &v) ? PDT_PRINTER
						     : (PrinterDriverType) v;
}

/* SpoolGetPrinterString: TRUE (carry) if there is no such printer */
Boolean SpoolGetPrinterString(PrinterStringInfo *retInfo, word printerNum)
{
    word len;

    if (InitFileReadStringSectionBuffer(printerCat, printersKey, printerNum,
					retInfo->PSI_stringBuf,
					GEODE_MAX_DEVICE_NAME_LENGTH, &len))
	return TRUE;
    retInfo->PSI_stringLength = len;
    retInfo->PSI_driverType = ReadPrinterDriverType(retInfo->PSI_stringBuf);
    return FALSE;
}

void SpoolGetPrinterInfo(void)
{
}

static Boolean SameName(const char *a, const char *b)
{
    while (*a && *a == *b) {
	a++;
	b++;
    }
    return *a == *b;
}

static Boolean DuplicateCB(const char *section, word num, void *data)
{
    (void) num;
    return SameName(section, (const char *) data);
}

/* SpoolCreatePrinter: the number of the new printer; 0xffff (the asm's
 * carry) if one of that name exists */
word SpoolCreatePrinter(const char *printerName, PrinterDriverType driverType)
{
    word n;

    if (InitFileEnumStringSection(printerCat, printersKey, IFRF_READ_ALL,
				  DuplicateCB, (void *) printerName))
	return 0xffff;
    InitFileWriteStringSection(printerCat, printersKey, printerName);
    AddCount(GetTypeKeyString(driverType), 1);
    n = (word) (AddCount(countKey, 1) - 1);
    InitFileWriteInteger(printerName, typeKey, driverType);
    SpoolSendPrintersChanged();
    return n;
}

void SpoolDeletePrinter(word printerNum)
{
    PrinterStringInfo psi;
    int def;

    if (SpoolGetPrinterString(&psi, printerNum))
	return;
    InitFileDeleteCategory(psi.PSI_stringBuf);
    InitFileDeleteStringSection(printerCat, printersKey, printerNum);
    AddCount(GetTypeKeyString(psi.PSI_driverType), -1);
    AddCount(countKey, -1);
    /* the default printer: the one before, or the first */
    if (psi.PSI_driverType == PDT_PRINTER) {
	def = SpoolGetDefaultPrinter();
	if (def >= (int) printerNum) {
	    if (def == (int) printerNum)
		def = 1;
	    SpoolSetDefaultPrinter((word) (def - 1));
	}
    }
    SpoolSendPrintersChanged();
}

int SpoolGetDefaultPrinter(void)
{
    word v;

    return InitFileReadInteger(printerCat, defaultKey, &v) ? 0 : (int) v;
}

void SpoolSetDefaultPrinter(word printerNum)
{
    InitFileWriteInteger(printerCat, defaultKey, printerNum);
    SpoolSendPrintersChanged();
}
