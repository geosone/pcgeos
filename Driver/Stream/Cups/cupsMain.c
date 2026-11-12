/*
 * cupsMain.c -- the CUPS port driver (cups.geo, cupsDr.h).
 *
 * A stream driver that the spooler opens as a custom port
 * (Library/Spool/Process/processCustom.asm).  The printer driver writes
 * its output to it as it would to a parallel port; this driver hands it
 * to a CUPS queue of the host through gapi_print (CInclude/gapi.h), so
 * the host's CUPS does the printing (Port32/GAPI-Concept.md §5).
 *
 * Unlike the stream drivers of DOS16 it has no stream buffer of the
 * Stream driver and no thread: what is written goes into a buffer of
 * CUPS_BUFFER_SIZE bytes per unit, and a full buffer goes to CUPS at
 * once.  The job is opened with its first data, so a port opened and
 * closed without output prints nothing.  An error stays with the unit
 * (DR_STREAM_GET_ERROR) and fails every later write.
 *
 * New in Port32; there is no asm original.
 */
#include <geos.h>
#include <driver.h>
#include <geosdriver.h>
#include <heap.h>
#include <initfile.h>
#include <streamC.h>
#include <string.h>
#include "ghal.h"
#include "gapi.h"
#include "cupsDr.h"

typedef struct {
    Boolean          used;
    gapi_print_job  *job;
    word             error;             /* 0 or CUPS_ERR_* */
    Boolean          duplex;
    word             len;               /* bytes in buf */
    char             queue[GAPI_PRINT_NAME_SIZE];
    byte             buf[CUPS_BUFFER_SIZE];
} CupsUnit;

static CupsUnit units[CUPS_NUM_UNITS];

/* what STREAM_ESC_LOAD_OPTIONS read, for the next DR_STREAM_OPEN */
static char    optQueue[GAPI_PRINT_NAME_SIZE];
static Boolean optDuplex;

/* ---------------------------------------------------------------------
 * Small helpers (a geode has no C library)
 * ------------------------------------------------------------------- */

static void CopyName(char *dst, const char *src, word size)
{
    word i;
    for (i = 0; i + 1 < size && src[i]; i++)
	dst[i] = src[i];
    dst[i] = 0;
}

static word ErrorOf(ghal_status st)
{
    switch (st) {
    case GHAL_ERR_ACCESS:
    case GHAL_ERR_NOTFOUND:
    case GHAL_ERR_INVAL:
	return CUPS_ERR_REFUSED;
    default:
	return CUPS_ERR_HOST;
    }
}

/* ---------------------------------------------------------------------
 * Sending
 * ------------------------------------------------------------------- */

/* the buffer to CUPS; the job begins with its first data */
static Boolean Flush(CupsUnit *u)
{
    ghal_status st;

    if (u->error)
	return TRUE;
    if (!u->len)
	return FALSE;
    if (!u->job) {
	st = gapi_print_job_open(u->queue, "GEOS", GAPI_DOC_POSTSCRIPT, 1,
				 u->duplex, &u->job);
	if (st != GHAL_OK) {
	    ghal_log(GHAL_LOG_WARN, "cups: no job on %s (%d: %s)", u->queue,
		     (int) st, gapi_last_error());
	    u->error = ErrorOf(st);
	    u->len = 0;
	    return TRUE;
	}
    }
    st = gapi_print_job_write(u->job, u->buf, u->len);
    u->len = 0;
    if (st != GHAL_OK) {
	ghal_log(GHAL_LOG_WARN, "cups: writing to %s failed (%d: %s)",
		 u->queue, (int) st, gapi_last_error());
	gapi_print_job_cancel(u->job);
	u->job = NULL;
	u->error = ErrorOf(st);
	return TRUE;
    }
    return FALSE;
}

static Boolean Put(CupsUnit *u, const byte *p, dword n)
{
    while (n) {
	dword k = CUPS_BUFFER_SIZE - u->len;
	if (k > n)
	    k = n;
	memcpy(u->buf + u->len, p, k);
	u->len += (word) k;
	p += k;
	n -= k;
	if (u->len == CUPS_BUFFER_SIZE && Flush(u))
	    return TRUE;
    }
    return FALSE;
}

/* ---------------------------------------------------------------------
 * The calls
 * ------------------------------------------------------------------- */

/* STREAM_ESC_LOAD_OPTIONS: ds:si = the printer's ini category */
static Boolean LoadOptions(const char *category)
{
    word n;
    Boolean b = FALSE;

    optQueue[0] = 0;
    optDuplex = FALSE;
    if (!category || !category[0])
	return FALSE;
    if (InitFileReadStringBuffer(category, "hostQueue", optQueue,
				 sizeof(optQueue), &n))
	optQueue[0] = 0;
    if (!InitFileReadBoolean(category, "hostDuplex", &b))
	optDuplex = b;
    return FALSE;
}

/* DR_STREAM_OPEN: bx = block with CPP_info (or 0) */
static Boolean Open(DriverRegs *r)
{
    MemHandle info = (MemHandle) r->DR_bx;
    CupsUnit *u = NULL;
    char queue[GAPI_PRINT_NAME_SIZE];
    word i;

    queue[0] = 0;
    if (info) {
	const char *p = MemLock(info);
	if (p) {
	    CopyName(queue, p, sizeof(queue));
	    MemUnlock(info);
	}
    }
    if (!queue[0])
	CopyName(queue, optQueue, sizeof(queue));
    if (!queue[0] && gapi_print_default(queue, sizeof(queue)) != GHAL_OK) {
	ghal_log(GHAL_LOG_WARN, "cups: no queue and no default printer");
	r->DR_ax = STREAM_NO_DEVICE;
	return TRUE;
    }
    for (i = 0; i < CUPS_NUM_UNITS; i++)
	if (!units[i].used) {
	    u = &units[i];
	    break;
	}
    if (!u) {
	r->DR_ax = STREAM_CANNOT_ALLOC;
	return TRUE;
    }
    u->used = TRUE;
    u->job = NULL;
    u->error = 0;
    u->len = 0;
    u->duplex = optDuplex;
    CopyName(u->queue, queue, sizeof(u->queue));
    r->DR_bx = i;
    return FALSE;
}

/* DR_STREAM_CLOSE: ax = STREAM_LINGER (print) or STREAM_DISCARD */
static void Close(CupsUnit *u, Boolean linger)
{
    uint32_t id = 0;
    ghal_status st;

    if (linger && !Flush(u) && u->job) {
	st = gapi_print_job_close(u->job, &id);
	if (st == GHAL_OK)
	    ghal_log(GHAL_LOG_INFO, "cups: job %lu on %s", (unsigned long) id,
		     u->queue);
	else
	    ghal_log(GHAL_LOG_WARN, "cups: %s did not take the job (%d: %s)",
		     u->queue, (int) st, gapi_last_error());
    } else if (u->job) {
	gapi_print_job_cancel(u->job);
    }
    u->job = NULL;
    u->len = 0;
    u->used = FALSE;
}

static Boolean CupsStrategy(word function, DriverRegs *r)
{
    CupsUnit *u = NULL;

    switch (function) {
    case DR_INIT:
    case DR_SUSPEND:
    case DR_UNSUSPEND:
	return FALSE;
    case DR_EXIT: {
	word i;
	for (i = 0; i < CUPS_NUM_UNITS; i++)
	    if (units[i].used)
		Close(&units[i], FALSE);
	return FALSE;
    }
    case DR_STREAM_GET_DEVICE_MAP:
	r->DR_ax = 0;			/* no devices of its own */
	return FALSE;
    case DR_STREAM_OPEN:
	return Open(r);
    case DRV_ESC_QUERY_ESC:
	if (r->DR_ax != STREAM_ESC_LOAD_OPTIONS &&
	    r->DR_ax != STREAM_ESC_RESTART_OUTPUT)
	    r->DR_ax = 0;
	return FALSE;
    case STREAM_ESC_LOAD_OPTIONS:
	return LoadOptions((const char *) r->DR_dsSi);
    default:
	break;
    }

    /* the rest is about an open unit */
    if (r->DR_bx >= CUPS_NUM_UNITS || !units[r->DR_bx].used) {
	r->DR_ax = STREAM_CLOSED;
	return TRUE;
    }
    u = &units[r->DR_bx];

    switch (function) {
    case DR_STREAM_CLOSE:
	Close(u, (sword) r->DR_ax == STREAM_LINGER);
	return FALSE;
    case DR_STREAM_SET_NOTIFY:		/* nothing to notify of */
    case DR_STREAM_SET_THRESHOLD:
	return FALSE;
    case DR_STREAM_GET_ERROR:
	r->DR_ax = u->error;
	return FALSE;
    case DR_STREAM_SET_ERROR:
	u->error = (word) r->DR_cx;
	return FALSE;
    case STREAM_ESC_RESTART_OUTPUT:	/* the user says go on */
	return FALSE;
    case DR_STREAM_FLUSH:		/* drop what is not sent yet */
	u->len = 0;
	return FALSE;
    case DR_STREAM_QUERY:		/* room to write: always */
	r->DR_ax = CUPS_BUFFER_SIZE - u->len;
	return FALSE;
    case DR_STREAM_WRITE:
	if (u->error || Put(u, r->DR_dsSi, (word) r->DR_cx)) {
	    r->DR_cx = 0;
	    r->DR_ax = STREAM_SHORT_READ_WRITE;
	    return TRUE;
	}
	return FALSE;			/* cx: all of it */
    case DR_STREAM_WRITE_BYTE: {
	byte c = (byte) r->DR_cx;
	if (u->error || Put(u, &c, 1)) {
	    r->DR_ax = STREAM_SHORT_READ_WRITE;
	    return TRUE;
	}
	return FALSE;
    }
    case DR_STREAM_READ:		/* a printer port: write only */
	r->DR_cx = 0;
	/* fall through */
    case DR_STREAM_READ_BYTE:
	r->DR_ax = STREAM_CLOSED;
	return TRUE;
    default:
	return TRUE;
    }
}

DriverInfoStruct DriverTable = {
    DRIVER_STRATEGY(CupsStrategy), DA_CHARACTER, DRIVER_TYPE_STREAM
};
