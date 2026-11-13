/*
 * processUtils.c -- the print queue's utilities (the C port of
 * processUtils.asm): AllocPrintQueue, FindRightQueue,
 * InsertJobIntoQueue, MapJobIDtoHandle, the job notifications and
 * AllocDeviceQueue.
 *
 * All of them run with the queue locked (LockQueue).  Chunks may move
 * when one is allocated, so pointers into the queue are taken again
 * after LMemAlloc.
 */
#include "spoolLocal.h"
#include <string.h>

/* AllocPrintQueue: the block for the queues, locked and owned (the
 * caller holds queueSemaphore) */
void AllocPrintQueue(void)
{
    MemHandle mh;
    PrintQueue *pq;

    mh = MemAllocSetOwner(GeodeGetCodeProcessHandle(), sizeof(PrintQueue),
			  HF_DYNAMIC | HF_SHARABLE,
			  HAF_ZERO_INIT | HAF_NO_ERR);
    HandleP(mh);
    MemLock(mh);
    LMemInitHeap(mh, LMEM_TYPE_GENERAL, 0, sizeof(PrintQueue), 2,
		 sizeof(QueueInfo) + sizeof(JobInfoStruct) + 64);
    queueHandle = mh;
    pq = PQ();
    pq->PQ_numQueues = 0;
    pq->PQ_numJobs = 0;
    pq->PQ_firstQueue = 0;
}

/* FindRightQueue: the queue for the port, 0 if there is none.  There is
 * one queue per serial or parallel port, one for all files and one for
 * nothing; custom ports compare their CPP_info. */
ChunkHandle FindRightQueue(const PrintPortInfo *port)
{
    ChunkHandle ch;
    QueueInfo *q;

    if (!PQ()->PQ_numQueues)
	return 0;
    for (ch = PQ()->PQ_firstQueue; ch; ch = q->QI_next) {
	q = QUEUE(ch);
	if (q->QI_portInfo.PPI_type != port->PPI_type)
	    continue;
	if (port->PPI_type == PPT_CUSTOM) {
	    if (!memcmp(q->QI_portInfo.PPI_params.PP_custom.CPP_info,
			port->PPI_params.PP_custom.CPP_info,
			sizeof(port->PPI_params.PP_custom.CPP_info)))
		return ch;
	    continue;
	}
	if (port->PPI_type > PPT_PARALLEL)
	    return ch;
	if (q->QI_portInfo.PPI_params.PP_serial.SPP_portNum ==
	    port->PPI_params.PP_serial.SPP_portNum)
	    return ch;
    }
    return 0;
}

/* InsertJobIntoQueue: the job at the end of the queue */
void InsertJobIntoQueue(ChunkHandle job, ChunkHandle queue)
{
    QueueInfo *q = QUEUE(queue);
    JobInfoStruct *j;
    ChunkHandle ch;

    if (!q->QI_curJob) {
	q->QI_curJob = job;
	q->QI_fileHan = 0;
	q->QI_filePos = 0;
	q->QI_curPage = 0;
	return;
    }
    for (ch = q->QI_curJob; (j = JOB(ch))->JIS_next; ch = j->JIS_next)
	;
    j->JIS_next = job;
}

/* MapJobIDtoHandle: the job's chunk, 0 if there is no such job */
ChunkHandle MapJobIDtoHandle(word jobID)
{
    ChunkHandle qch, jch;
    QueueInfo *q;

    for (qch = PQ()->PQ_firstQueue; qch; qch = q->QI_next) {
	q = QUEUE(qch);
	for (jch = q->QI_curJob; jch; jch = JOB(jch)->JIS_next)
	    if (JOB(jch)->JIS_jobID == jobID)
		return jch;
    }
    return 0;
}

/* SendJobNotification: MSG_SPOOL_JOB_ADDED or _REMOVED to the process,
 * and for a removed job the print job status to the system GCN list */
static void SendJobNotification(ChunkHandle job, Boolean added)
{
    JobInfoStruct *j = JOB(job);
    QueueInfo *q = QUEUE(j->JIS_queue);
    word id = j->JIS_jobID;
    word type;

    SpoolSendJobNotification(added, id, q->QI_portInfo.PPI_type,
			     (word) q->QI_portInfo.PPI_params.PP_serial.
			     SPP_portNum);
    if (added)
	return;
    switch (q->QI_error) {
    case SI_KEEP_GOING:
	type = SPOOL_PSCT_COMPLETE;
	break;
    case SI_ERROR:
	type = SPOOL_PSCT_ERROR;
	break;
    case SI_DETACH:
	/* only if the job is canceled by the detach; a saved job is just
	 * suspended */
	if (!(JOB(job)->JIS_info.JP_spoolOpts & SO_SHUTDOWN_ACTION))
	    return;
	/* fall through */
    default:
	type = SPOOL_PSCT_CANCELED;
	break;
    }
    SpoolSendStatusChange(type, id, 0);
}

void SendJobAddedNotification(ChunkHandle job)
{
    SendJobNotification(job, TRUE);
}

void SendJobRemovedNotification(ChunkHandle job)
{
    SendJobNotification(job, FALSE);
}

/* AllocDeviceQueueLow: the queue chunk for the job's port, at the end of
 * the queue list */
static ChunkHandle AllocDeviceQueueLow(ChunkHandle job)
{
    ChunkHandle ch = LMemAlloc(queueHandle, sizeof(QueueInfo)), c;
    QueueInfo *q = QUEUE(ch), *p;
    JobInfoStruct *j = JOB(job);

    memset(q, 0, sizeof(*q));
    memcpy(q->QI_device, j->JIS_info.JP_deviceName, MAX_DEVICE_NAME_SIZE);
    q->QI_portInfo = j->JIS_info.JP_portInfo;
    /* a custom port gets a unit of its own, so the job notifications
     * can tell the queues apart */
    if (j->JIS_info.JP_portInfo.PPI_type == PPT_CUSTOM)
	q->QI_portInfo.PPI_params.PP_custom.CPP_unit = customQueueID++;
    q->QI_error = SI_KEEP_GOING;

    if (!PQ()->PQ_firstQueue)
	PQ()->PQ_firstQueue = ch;
    else {
	for (c = PQ()->PQ_firstQueue; (p = QUEUE(c))->QI_next;
	     c = p->QI_next)
	    ;
	p->QI_next = ch;
    }
    PQ()->PQ_numQueues++;
    return ch;
}

/* AllocDeviceQueueError: the asm asked the user to try again; without
 * the UI the queue is given up (as on IC_DISMISS) */
static void AllocDeviceQueueError(ChunkHandle queue)
{
    ChunkHandle c;
    QueueInfo *p;

    SpoolErrorBox(SERROR_CANT_ALLOC_BITMAP, 0);
    if (--PQ()->PQ_numQueues == 0)
	PQ()->PQ_firstQueue = 0;
    else if (PQ()->PQ_firstQueue == queue)
	PQ()->PQ_firstQueue = QUEUE(queue)->QI_next;
    else {
	for (c = PQ()->PQ_firstQueue; (p = QUEUE(c))->QI_next != queue;
	     c = p->QI_next)
	    ;
	p->QI_next = QUEUE(queue)->QI_next;
    }
    LMemFreeHandles(queueHandle, queue);
}

/* AllocDeviceQueue: a queue and its thread for the job's port; 0 if no
 * thread could be made.  The thread starts by locking the queue, which
 * the caller holds until all is set up. */
ChunkHandle AllocDeviceQueue(ChunkHandle job)
{
    ChunkHandle ch = AllocDeviceQueueLow(job);
    ThreadHandle th;

    th = ThreadCreate(PRIORITY_LOW, ch, SpoolThreadBirth,
		      (word) (2048 + sizeof(SpoolJobInfo)),
		      GeodeGetCodeProcessHandle());
    if (!th) {
	TimerSleep(30);
	th = ThreadCreate(PRIORITY_LOW, ch, SpoolThreadBirth,
			  (word) (2048 + sizeof(SpoolJobInfo)),
			  GeodeGetCodeProcessHandle());
    }
    if (!th) {
	AllocDeviceQueueError(ch);
	return 0;
    }
    QUEUE(ch)->QI_thread = th;
    return ch;
}
