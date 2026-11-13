/*
 * processLoop.c -- a queue's thread (the C port of processLoop.asm):
 * it prints the queue's jobs one after the other and goes away with
 * the last; the last queue frees the print queue.
 *
 * Port32: no heap reservation (GeodeRequestSpace: the C kernel's heap
 * has no fixed size).
 */
#include "spoolLocal.h"
#include <string.h>

/* LoadPrinterDriver: the printer's driver and device from its category
 * of the ini file (UserLoadExtendedDriver of the UI library: "driver"
 * and "device"), set to the device with the PState; the previous job's
 * driver is freed.  TRUE (carry) on an error. */
static Boolean LoadPrinterDriver(SpoolJobInfo *j)
{
    char name[FILE_LONGNAME_BUFFER_SIZE];
    char device[GEODE_MAX_DEVICE_NAME_SIZE];
    const char *cat = j->SJI_info.JP_printerName;
    GeodeLoadError e;
    GeodeHandle gh = 0, prev;
    DriverInfoStruct *dis;
    DriverRegs r;
    word size;

    if (!InitFileReadStringBuffer(cat, "driver", name, sizeof(name),
				  &size) &&
	!InitFileReadStringBuffer(cat, "device", device, sizeof(device),
				  &size)) {
	FilePushDir();
	FileSetStandardPath(SP_PRINTER_DRIVERS);
	gh = GeodeUseDriver(name, PRINT_PROTO_MAJOR, PRINT_PROTO_MINOR, &e);
	FilePopDir();
    }
    if (gh) {
	dis = GeodeInfoDriver(gh);
	memset(&r, 0, sizeof(r));
	r.DR_dsSi = device;
	if (!dis || DriverCall(dis, DRE_TEST_DEVICE, &r) ||
	    r.DR_ax == DP_NOT_PRESENT) {
	    GeodeFreeDriver(gh);
	    gh = 0;
	} else {
	    memset(&r, 0, sizeof(r));
	    r.DR_dsSi = device;
	    r.DR_bx = j->SJI_pstate;
	    if (DriverCall(dis, DRE_SET_DEVICE, &r)) {
		GeodeFreeDriver(gh);
		gh = 0;
	    }
	}
    }
    if (!gh) {
	SpoolErrorBox(SERROR_NO_PRINT_DRIVER, j->SJI_qHan);
	return TRUE;
    }
    j->SJI_pdHan = gh;
    LockQueue();
    QUEUE(j->SJI_qHan)->QI_pdHan = gh;
    UnlockQueue();
    prev = j->SJI_prev_pdHan;
    j->SJI_prev_pdHan = gh;
    if (prev)
	GeodeFreeDriver(prev);
    j->SJI_pDriver = DriverStrategyOf(GeodeInfoDriver(gh));
    return FALSE;
}

/* SpoolerLoop: the jobs of the queue; returns with the queue locked
 * and gone */
static void SpoolerLoop(ChunkHandle queue)
{
    SpoolJobInfo curJob, *j = &curJob;
    ChunkHandle ch;
    QueueInfo *p;
    Boolean b;

    memset(j, 0, sizeof(*j));
    j->SJI_qHan = queue;

    /* [spool] noPrinting: wait (used by "Print to Screen") */
    while (!InitFileReadBoolean("spool", "noPrinting", &b) && b)
	TimerSleep(60 * 15);

    j->SJI_pstate = MemAllocSetOwner(GeodeGetCodeProcessHandle(),
				     sizeof(PState) + sizeof(JobParameters),
				     HF_DYNAMIC | HF_SHARABLE,
				     HAF_NO_ERR | HAF_ZERO_INIT);
    do {
	GetJobInfo(j);
	if (!LoadPrinterDriver(j) && !LoadPortDriver(j))
	    PrintFile(j);
	else
	    StoppedByError(j);
    } while (!RemoveJob(j));

    /* the queue is locked: one queue less, or none */
    MemFree(j->SJI_pstate);
    if (--PQ()->PQ_numQueues == 0) {
	MemHandle mh = queueHandle;
	MemUnlockV(mh);
	queueHandle = 0;
	MemFree(mh);
    } else {
	if (PQ()->PQ_firstQueue == queue)
	    PQ()->PQ_firstQueue = QUEUE(queue)->QI_next;
	else {
	    for (ch = PQ()->PQ_firstQueue; (p = QUEUE(ch))->QI_next != queue;
		 ch = p->QI_next)
		;
	    p->QI_next = QUEUE(queue)->QI_next;
	}
	LMemFreeHandles(queueHandle, queue);
    }
    UnlockQueue();
    if (j->SJI_sHan)
	GeodeFreeDriver(j->SJI_sHan);
    if (j->SJI_pdHan)
	GeodeFreeDriver(j->SJI_pdHan);
}

/* SpoolThreadBirth: the start routine of a queue's thread */
word SpoolThreadBirth(word queue)
{
    SpoolerLoop((ChunkHandle) queue);
    return 0;
}
