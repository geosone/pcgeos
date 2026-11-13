/*
 * libHost.c -- the host's printers in the printer list (new in Port32;
 * Port32/GAPI-Concept.md §5, Port32/ABI.md §13w).
 *
 * At the spooler's start the printers of the host (CUPS, through
 * gapi_print) become printers of the ini file, as if they had been
 * installed in Preferences: the PostScript driver with a PostScript
 * device (a color one for a color printer), the custom port with the
 * CUPS port driver, the queue in hostQueue, duplex in hostDuplex.
 * Such a printer is marked hostPrinter; one whose queue the host no
 * longer has is deleted again.  The printer's name is the name the user
 * sees on the host (printer-info), else the queue's; if a printer of the
 * ini has that name already, the queue's name is taken.  Without a
 * default printer in the ini, the host's default becomes it.
 *
 * Nothing changes when the host cannot be asked (dos32, no CUPS): the
 * host printers of the last start stay.  [spool] hostPrinters = false
 * turns it off.
 */
#include "spoolLocal.h"
#include "gapi.h"

#define HOST_MAX_PRINTERS	32

static const char printerCat[] = "printer";
static const char hostKey[] = "hostPrinter";
static const char queueKey[] = "hostQueue";

/* the device of the PostScript driver (psdevinfo2c.py's names) */
static const char monoDevice[] = "PostScript Compatible";
static const char colorDevice[] = "HP Color LaserJet (PostScript)";

static Boolean Same(const char *a, const char *b)
{
    while (*a && *a == *b) {
	a++;
	b++;
    }
    return *a == *b;
}

static void Copy(char *dst, const char *src, word size)
{
    word i;

    for (i = 0; i + 1 < size && src[i]; i++)
	dst[i] = src[i];
    dst[i] = 0;
}

/* the number of the printer of that name; -1 if there is none */
static int FindPrinter(const char *name)
{
    PrinterStringInfo psi;
    word i;

    for (i = 0; !SpoolGetPrinterString(&psi, i); i++)
	if (Same(psi.PSI_stringBuf, name))
	    return (int) i;
    return -1;
}

/* TRUE if the printer is one of the host's, with its queue in queue */
static Boolean IsHostPrinter(const char *printer, char *queue, word size)
{
    Boolean b = FALSE;
    word n;

    if (InitFileReadBoolean(printer, hostKey, &b) || !b)
	return FALSE;
    if (InitFileReadStringBuffer(printer, queueKey, queue, size, &n))
	queue[0] = 0;
    return TRUE;
}

/* the keys of a host printer */
static void WriteHostPrinter(const char *name, const gapi_printer *p)
{
    InitFileWriteString(name, "driver", "pscript.geo");
    InitFileWriteString(name, "device", p->color ? colorDevice
						  : monoDevice);
    InitFileWriteString(name, "port", "CUSTOM");
    InitFileWriteString(name, "portDriver", "cups.geo");
    InitFileWriteString(name, queueKey, p->name);
    InitFileWriteBoolean(name, "hostDuplex", p->duplex ? TRUE : FALSE);
    InitFileWriteBoolean(name, hostKey, TRUE);
}

void SpoolSyncHostPrinters(void)
{
    PrinterStringInfo psi;
    char queue[GAPI_PRINT_NAME_SIZE], name[GEODE_MAX_DEVICE_NAME_SIZE];
    MemHandle mh;
    gapi_printer *list;
    Boolean b, hadDefault;
    int count = 0, i, k, num;
    word v;

    if (!InitFileReadBoolean("spool", "hostPrinters", &b) && !b)
	return;
    mh = MemAlloc(HOST_MAX_PRINTERS * sizeof(gapi_printer), HF_DYNAMIC,
		  HAF_LOCK);
    if (!mh)
	return;
    list = MemDeref(mh);
    if (gapi_print_list(list, HOST_MAX_PRINTERS, &count) != GHAL_OK) {
	MemFree(mh);
	return;
    }
    if (count > HOST_MAX_PRINTERS)
	count = HOST_MAX_PRINTERS;
    hadDefault = !InitFileReadInteger(printerCat, "defaultPrinter", &v);

    /* the host printers the host no longer has */
    for (num = 0; !SpoolGetPrinterString(&psi, (word) num); ) {
	if (IsHostPrinter(psi.PSI_stringBuf, queue, sizeof(queue))) {
	    for (k = 0; k < count && !Same(list[k].name, queue); k++)
		;
	    if (k == count) {
		ghal_log(GHAL_LOG_INFO, "spool: host printer %s is gone",
			 psi.PSI_stringBuf);
		SpoolDeletePrinter((word) num);
		continue;
	    }
	}
	num++;
    }

    /* the host's printers: updated, or new */
    for (i = 0; i < count; i++) {
	num = -1;
	for (k = 0; !SpoolGetPrinterString(&psi, (word) k); k++)
	    if (IsHostPrinter(psi.PSI_stringBuf, queue, sizeof(queue)) &&
		Same(queue, list[i].name)) {
		num = k;
		break;
	    }
	if (num >= 0) {
	    WriteHostPrinter(psi.PSI_stringBuf, &list[i]);
	} else {
	    Copy(name, list[i].info[0] ? list[i].info : list[i].name,
		 GEODE_MAX_DEVICE_NAME_LENGTH);
	    if (FindPrinter(name) >= 0)
		Copy(name, list[i].name, GEODE_MAX_DEVICE_NAME_LENGTH);
	    if (FindPrinter(name) >= 0)
		continue;			/* the user's printer */
	    num = (int) SpoolCreatePrinter(name, PDT_PRINTER);
	    if (num == 0xffff)
		continue;
	    WriteHostPrinter(name, &list[i]);
	    num = FindPrinter(name);
	    ghal_log(GHAL_LOG_INFO, "spool: host printer %s (%s)", name,
		     list[i].name);
	}
	if (!hadDefault && list[i].isDefault && num >= 0) {
	    SpoolSetDefaultPrinter((word) num);
	    hadDefault = TRUE;
	}
    }
    MemFree(mh);
}
