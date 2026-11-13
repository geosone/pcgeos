/*
 * filestrMain.c -- the file stream driver (filestr.geo) in C: the port
 * the spooler prints to a file through (Library/Spool/Process/
 * processFile.c).
 *
 * The C port of filestrMain.asm, but self-contained: the asm opened a
 * stream of the Stream driver for each file and wrote it from the
 * stream's notification; this driver has no thread and no Stream
 * driver.  What is written goes into a buffer of the unit (the size
 * DR_STREAM_OPEN is passed in dx), a full buffer goes to the file at
 * once with FileWrite, and DR_STREAM_CLOSE with STREAM_LINGER writes
 * the rest (STREAM_DISCARD drops it).  The file belongs to the caller:
 * it is opened before DR_STREAM_OPEN and closed after DR_STREAM_CLOSE.
 * A write error stays with the unit (DR_STREAM_GET_ERROR) and fails
 * every later write; it is STREAM_SHORT_READ_WRITE, as the asm's
 * notification reported it.  Reads fail: the file is written only.
 */
#include <geos.h>
#include <driver.h>
#include <geosdriver.h>
#include <heap.h>
#include <file.h>
#include <streamC.h>
#include <Internal/streamDr.h>

#define FILESTR_NUM_UNITS	4	/* NUM_FSD_ENTRIES: plenty */
#define FILESTR_MIN_BUFFER	128
#define FILESTR_MAX_BUFFER	32768

typedef struct {
    Boolean	used;
    FileHandle	file;		/* FSD_file */
    MemHandle	mem;		/* the buffer */
    byte	*buf;
    word	size;		/* of buf */
    word	len;		/* bytes in buf */
    word	error;		/* 0 or a StreamError */
} FilestrUnit;

static FilestrUnit units[FILESTR_NUM_UNITS];

/* Flush: the buffer to the file; TRUE on an error */
static Boolean Flush(FilestrUnit *u)
{
    word n = u->len;

    u->len = 0;
    if (!n || u->error)
	return u->error != 0;
    if (FileWrite(u->file, u->buf, n, FALSE) != n) {
	u->error = STREAM_SHORT_READ_WRITE;
	return TRUE;
    }
    return FALSE;
}

static Boolean Put(FilestrUnit *u, const byte *p, word n)
{
    word room;

    while (n) {
	if (u->len == u->size && Flush(u))
	    return TRUE;
	room = (word) (u->size - u->len);
	if (room > n)
	    room = n;
	{
	    word i;
	    for (i = 0; i < room; i++)
		u->buf[u->len + i] = p[i];
	}
	u->len = (word) (u->len + room);
	p += room;
	n = (word) (n - room);
    }
    return FALSE;
}

/* DR_STREAM_OPEN: bx = the file, dx = buffer size; returns bx = unit */
static Boolean Open(DriverRegs *r)
{
    FilestrUnit *u = NULL;
    word size = (word) r->DR_dx, i;

    if (!r->DR_bx) {
	r->DR_ax = STREAM_NO_DEVICE;
	return TRUE;
    }
    for (i = 0; i < FILESTR_NUM_UNITS; i++)
	if (!units[i].used) {
	    u = &units[i];
	    break;
	}
    if (!u) {
	r->DR_ax = STREAM_CANNOT_ALLOC;
	return TRUE;
    }
    if (size < FILESTR_MIN_BUFFER)
	size = FILESTR_MIN_BUFFER;
    if (size > FILESTR_MAX_BUFFER)
	size = FILESTR_MAX_BUFFER;
    u->mem = MemAlloc(size, HF_DYNAMIC, HAF_LOCK);
    if (!u->mem) {
	r->DR_ax = STREAM_CANNOT_ALLOC;
	return TRUE;
    }
    u->buf = MemDeref(u->mem);
    u->used = TRUE;
    u->file = (FileHandle) r->DR_bx;
    u->size = size;
    u->len = 0;
    u->error = 0;
    r->DR_bx = i;
    return FALSE;
}

/* DR_STREAM_CLOSE: ax = STREAM_LINGER (write the rest) or
 * STREAM_DISCARD */
static void Close(FilestrUnit *u, Boolean linger)
{
    if (linger)
	(void) Flush(u);
    MemFree(u->mem);
    u->mem = 0;
    u->buf = NULL;
    u->len = 0;
    u->used = FALSE;
}

static Boolean FilestrStrategy(word function, DriverRegs *r)
{
    FilestrUnit *u;

    switch (function) {
    case DR_INIT:
    case DR_SUSPEND:
    case DR_UNSUSPEND:
	return FALSE;
    case DR_EXIT: {
	word i;
	for (i = 0; i < FILESTR_NUM_UNITS; i++)
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
	r->DR_ax = 0;			/* no escapes */
	return FALSE;
    default:
	break;
    }

    /* the rest is about an open unit */
    if (r->DR_bx >= FILESTR_NUM_UNITS || !units[r->DR_bx].used) {
	r->DR_ax = STREAM_CLOSED;
	return TRUE;
    }
    u = &units[r->DR_bx];

    switch (function) {
    case DR_STREAM_CLOSE:
	Close(u, (sword) r->DR_ax == STREAM_LINGER);
	return FALSE;
    case DR_STREAM_SET_NOTIFY:		/* errors stay with the unit */
    case DR_STREAM_SET_THRESHOLD:
	return FALSE;
    case DR_STREAM_GET_ERROR:
	r->DR_ax = u->error;
	return FALSE;
    case DR_STREAM_SET_ERROR:
	u->error = (word) r->DR_cx;
	return FALSE;
    case DR_STREAM_FLUSH:		/* drop what is not written yet */
	u->len = 0;
	return FALSE;
    case DR_STREAM_QUERY:		/* room to write */
	r->DR_ax = (word) (u->size - u->len);
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
    case DR_STREAM_READ:		/* written only */
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
    DRIVER_STRATEGY(FilestrStrategy), DA_CHARACTER, DRIVER_TYPE_STREAM
};
