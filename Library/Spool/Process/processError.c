/*
 * processError.c -- the spooler's errors (the C port of SpoolErrorBox of
 * processError.asm).
 *
 * Port32: the error boxes need the UI library, which the spooler does
 * not use yet.  SpoolErrorBox keeps what the asm did around the box (one
 * error at a time, the queue paused while it is up) and logs the error
 * instead (ghal_log).  The answer is what a user would most likely give:
 * IC_OK (go on) for the manual paper feed, else IC_DISMISS (stop the
 * job), so no error is retried endlessly.  With the UI, the box comes
 * back here (SpoolErrorBoxUI below is the hook).
 */
#include "spoolLocal.h"
#include "ghal.h"

static const char *ErrorName(word error)
{
    switch (error) {
    case PERROR_PARALLEL_ERR:		return "cannot open the port";
    case PERROR_FILE_SYSTEM_FULL:	return "the disk is full";
    case PERROR_FILE_SYSTEM_ERROR:	return "cannot write the file";
    case SERROR_NO_SPOOL_FILE:		return "no spool file";
    case SERROR_NO_PRINT_DRIVER:	return "cannot load the printer driver";
    case SERROR_NO_PORT_DRIVER:		return "cannot load the port driver";
    case SERROR_NO_PRINTERS:		return "no printers";
    case SERROR_NO_MODE_AVAIL:		return "the print mode is not supported";
    case SERROR_CANT_ALLOC_BITMAP:	return "out of memory";
    case SERROR_MANUAL_PAPER_FEED:	return "insert the next sheet";
    case SERROR_CANT_LOAD_PORT_DRIVER:	return "cannot load the port driver";
    case SERROR_CANNOT_OPEN_FILE:	return "cannot open the output file";
    case SERROR_CANNOT_CONVERT_PAGE:	return "the driver could not convert a page";
    case SERROR_RESERVATION_ERROR:	return "out of heap space";
    default:				return "printer error";
    }
}

/* the queue's pause flag while the error is up (PauseSpoolThread) */
static byte SetPause(ChunkHandle queue, byte e)
{
    byte old = SI_KEEP_GOING;
    QueueInfo *q;

    if (!queue || LockQueue()) {
	if (queue)
	    UnlockQueue();
	return old;
    }
    q = QUEUE(queue);
    old = q->QI_error;
    if (e != SI_PAUSE || old == SI_KEEP_GOING)
	q->QI_error = e;
    UnlockQueue();
    return old;
}

word SpoolErrorBox(word error, ChunkHandle queue)
{
    char device[MAX_DEVICE_NAME_SIZE + 1];
    word answer, i;
    byte old;

    ThreadGrabThreadLock(errorThreadLock);
    errorBoxCount++;
    old = SetPause(queue, SI_PAUSE);
    device[0] = 0;
    if (queue && !LockQueue()) {
	const char *d = QUEUE(queue)->QI_device;
	for (i = 0; i < MAX_DEVICE_NAME_SIZE && d[i]; i++)
	    device[i] = d[i];
	device[i] = 0;
    }
    if (queue)
	UnlockQueue();

    answer = error == SERROR_MANUAL_PAPER_FEED ? SPOOL_IC_OK
					       : SPOOL_IC_DISMISS;
    ghal_log(error == SERROR_MANUAL_PAPER_FEED ? GHAL_LOG_INFO
					       : GHAL_LOG_WARN,
	     "spool: %s%s%s (SL-%02x)", device, device[0] ? ": " : "",
	     ErrorName(error), (unsigned) error);

    /* the pause is over; the job goes on or stops as answered */
    if (old == SI_KEEP_GOING)
	SetPause(queue, SI_KEEP_GOING);
    errorBoxCount--;
    ThreadReleaseThreadLock(errorThreadLock);
    return answer;
}
