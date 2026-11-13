/*
 * processPort.c -- the port a queue prints to (the C port of
 * processPort.asm): loading its stream driver, opening and closing the
 * port, and SpoolVerifyPrinterPort.
 *
 * The port drivers are those of processTables.asm, from the system
 * directory: serial.geo, parallel.geo, filestr.geo; a custom port names
 * its driver in the printer's category (key "portDriver"), the nothing
 * port needs none.  Port32: the non-EC names only (one build), and
 * serial and parallel ports wait for their stream drivers.  The
 * StreamErrorHandler notification of the asm is not used: the C port
 * drivers keep a write error with the unit and fail the writes, so the
 * printer driver's call fails and the job ends with an error.
 */
#include "spoolLocal.h"
#include <string.h>

static const char *const portNames[] = {
    "serial.geo",		/* PPT_SERIAL */
    "parallel.geo",		/* PPT_PARALLEL */
    "filestr.geo",		/* PPT_FILE */
    NULL,			/* PPT_NOTHING */
    NULL			/* PPT_CUSTOM: "portDriver" of the printer */
};

/* LoadPortDriverCommon: the port's driver, 0 if it cannot be loaded */
static GeodeHandle LoadPortDriverCommon(PrinterPortType type,
					const char *category)
{
    char name[FILE_LONGNAME_BUFFER_SIZE];
    const char *n = NULL;
    GeodeLoadError e;
    GeodeHandle gh = 0;
    word size;

    if ((word) type / 2 < sizeof(portNames) / sizeof(portNames[0]))
	n = portNames[type / 2];
    if (!n) {
	if (!category ||
	    InitFileReadStringBuffer(category, "portDriver", name,
				     sizeof(name), &size))
	    return 0;
	n = name;
    }
    FilePushDir();
    FileSetStandardPath(SP_SYSTEM);
    gh = GeodeUseDriver(n, 0, 0, &e);
    FilePopDir();
    return gh;
}

/* LoadPortDriver: once per queue; TRUE (carry) on an error */
Boolean LoadPortDriver(SpoolJobInfo *j)
{
    GeodeHandle gh = 0;

    if (j->SJI_sHan)
	return FALSE;
    if (j->SJI_info.JP_portInfo.PPI_type != PPT_NOTHING) {
	gh = LoadPortDriverCommon(j->SJI_info.JP_portInfo.PPI_type,
				  j->SJI_info.JP_printerName);
	if (!gh) {
	    SpoolErrorBox(SERROR_NO_PORT_DRIVER, j->SJI_qHan);
	    return TRUE;
	}
    }
    j->SJI_sHan = gh;
    LockQueue();
    QUEUE(j->SJI_qHan)->QI_portHan = gh;
    UnlockQueue();
    return FALSE;
}

/* InitPrinterPort: TRUE and a PortErrors on an error */
Boolean InitPrinterPort(SpoolJobInfo *j, word *portError)
{
    if (j->SJI_sHan)
	j->SJI_stream = DriverStrategyOf(GeodeInfoDriver(j->SJI_sHan));
    switch (j->SJI_info.JP_portInfo.PPI_type) {
    case PPT_FILE:
	return InitFilePort(j, portError);
    case PPT_CUSTOM:
	return InitCustomPort(j, portError);
    case PPT_NOTHING:
	return InitNothingPort(j, portError);
    default:
	/* serial, parallel: no stream drivers yet */
	*portError = PE_PORT_NOT_OPENED;
	return TRUE;
    }
}

void ClosePrinterPort(SpoolJobInfo *j)
{
    switch (j->SJI_info.JP_portInfo.PPI_type) {
    case PPT_FILE:
	ExitFilePort(j);
	break;
    case PPT_CUSTOM:
	ExitCustomPort(j);
	break;
    case PPT_NOTHING:
	ExitNothingPort(j);
	break;
    default:
	break;
    }
}

/* SpoolVerifyPrinterPort: file, nothing and custom ports need no check
 * (VerifyFilePort was empty); serial and parallel ports cannot be
 * checked without their drivers */
SpoolOpStatus SpoolVerifyPrinterPort(PrintPortInfo *portInfo)
{
    GeodeHandle gh;

    if (portInfo->PPI_type >= PPT_NOTHING)
	return SPOOL_OPERATION_SUCCESSFUL;
    gh = LoadPortDriverCommon(portInfo->PPI_type, NULL);
    if (!gh) {
	SpoolErrorBox(SERROR_CANT_LOAD_PORT_DRIVER, 0);
	return SPOOL_CANT_VERIFY_PORT;
    }
    GeodeFreeDriver(gh);
    if (portInfo->PPI_type == PPT_FILE)
	return SPOOL_OPERATION_SUCCESSFUL;
    return SPOOL_CANT_VERIFY_PORT;
}
