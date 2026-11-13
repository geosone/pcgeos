/*
 * processQueue.c -- the spooler's queue routines (the C port of
 * processQueue.asm): SpoolAddJob, SpoolDelJob, SpoolInfo, SpoolHurryJob,
 * SpoolDelayJob, SpoolModifyPriority and
 * SpoolQueueCheckJobsWantingWarning.
 *
 * They run on the caller's thread and lock the queue (LockQueue).
 * Port32 differences: SpoolAddJob returns SPOOL_INVALID_JOB_ID when the
 * spooler is detaching and the job is dropped (the asm left cx as the
 * unused ID); SpoolHurryJob of the job being printed fails instead of
 * linking the job to itself.
 */
#include "spoolLocal.h"
#include <string.h>

/* SpoolAddJob: job IDs are unique for the life of the spooler */
word SpoolAddJob(const JobParameters *params)
{
    word id;

    ThreadPSem(queueSemaphore);
    id = nextJobID++;
    if (nextJobID == SPOOL_INVALID_JOB_ID)
	nextJobID = 0;
    ThreadVSem(queueSemaphore);
    return SpoolAddJobInternal(params, id);
}

/* SpoolAddJobInternal: the job into its queue; if there is no queue for
 * its port, the process makes one (MSG_SPOOL_CREATE_THREAD) */
word SpoolAddJobInternal(const JobParameters *params, word jobID)
{
    ChunkHandle job, queue;
    JobInfoStruct *j;
    TimerDateAndTime now;
    word size = params->JP_size;

    if (size < offsetof(JobParameters, JP_printerData))
	size = sizeof(JobParameters);

    ThreadPSem(queueSemaphore);
    if (spoolStateFlags & SS_DETACHING) {
	ThreadVSem(queueSemaphore);
	return SPOOL_INVALID_JOB_ID;
    }
    if (!queueHandle)
	AllocPrintQueue();
    else
	MemPLock(queueHandle);

    /* at least a whole JobParameters, so the threads can read every
     * field */
    job = LMemAlloc(queueHandle,
		    JIS_SIZE(size < sizeof(JobParameters)
			     ? sizeof(JobParameters) : size));
    j = JOB(job);
    memset(j, 0, JIS_SIZE(sizeof(JobParameters)));
    memcpy(&j->JIS_info, params, size);
    j->JIS_info.JP_size = size;
    TimerGetDateAndTime(&now);
    j->JIS_time.STS_hour = (byte) now.TDAT_hours;
    j->JIS_time.STS_minute = (byte) now.TDAT_minutes;
    j->JIS_time.STS_second = (byte) now.TDAT_seconds;
    j->JIS_jobID = jobID;

    queue = FindRightQueue(&JOB(job)->JIS_info.JP_portInfo);
    if (queue) {
	JOB(job)->JIS_queue = queue;
	InsertJobIntoQueue(job, queue);
	SendJobAddedNotification(job);
	PQ()->PQ_numJobs++;
    } else
	SpoolSendCreateThread(job);
    UnlockQueue();
    return jobID;
}

/* DeleteSpoolFile: the job's spool file, from the spool directory */
static void DeleteSpoolFile(const char *fname)
{
    FilePushDir();
    FileSetStandardPath(SP_SPOOL);
    FileDelete(fname);
    FilePopDir();
}

SpoolOpStatus SpoolDelJob(word jobID)
{
    ChunkHandle job, ch;
    QueueInfo *q;
    JobInfoStruct *p;
    SpoolOpStatus st;

    if (LockQueue()) {
	UnlockQueue();
	return SPOOL_QUEUE_EMPTY;
    }
    job = MapJobIDtoHandle(jobID);
    if (!job) {
	UnlockQueue();
	return SPOOL_JOB_NOT_FOUND;
    }
    q = QUEUE(JOB(job)->JIS_queue);
    if (q->QI_curJob == job) {
	/* the thread is printing it: tell it to stop */
	q->QI_error = SI_ABORT;
	UnlockQueue();
	return SPOOL_OPERATION_SUCCESSFUL;
    }
    st = SPOOL_JOB_NOT_FOUND;
    for (ch = q->QI_curJob; ch; ch = p->JIS_next) {
	p = JOB(ch);
	if (p->JIS_next != job)
	    continue;
	p->JIS_next = JOB(job)->JIS_next;
	SendJobRemovedNotification(job);
	if (JOB(job)->JIS_info.JP_spoolOpts & SO_DELETE)
	    DeleteSpoolFile(JOB(job)->JIS_info.JP_fname);
	LMemFreeHandles(queueHandle, job);
	PQ()->PQ_numJobs--;
	st = SPOOL_OPERATION_SUCCESSFUL;
	break;
    }
    UnlockQueue();
    return st;
}

/* SpoolInfoJob: a block with the job's JobStatus or JobParameters */
static SpoolOpStatus SpoolInfoJob(SpoolInfoType type, word jobID,
				  MemHandle *ret)
{
    ChunkHandle job;
    JobInfoStruct *j;
    QueueInfo *q;
    MemHandle mh;
    JobStatus *js;

    if (LockQueue()) {
	UnlockQueue();
	return SPOOL_QUEUE_EMPTY;
    }
    job = MapJobIDtoHandle(jobID);
    if (!job) {
	UnlockQueue();
	return SPOOL_JOB_NOT_FOUND;
    }
    j = JOB(job);
    q = QUEUE(j->JIS_queue);
    /* a job being aborted is gone already */
    if (q->QI_error == SI_ABORT && q->QI_curJob == job) {
	UnlockQueue();
	return SPOOL_JOB_NOT_FOUND;
    }
    if (type == SIT_JOB_PARAMETERS) {
	mh = MemAlloc(j->JIS_info.JP_size, HF_DYNAMIC | HF_SHARABLE,
		      HAF_NO_ERR | HAF_LOCK);
	memcpy(MemDeref(mh), &JOB(job)->JIS_info, JOB(job)->JIS_info.JP_size);
    } else {
	mh = MemAlloc(sizeof(JobStatus), HF_DYNAMIC | HF_SHARABLE,
		      HAF_NO_ERR | HAF_LOCK | HAF_ZERO_INIT);
	js = MemDeref(mh);
	j = JOB(job);
	q = QUEUE(j->JIS_queue);
	memcpy(js->JS_fname, j->JIS_info.JP_fname, sizeof(js->JS_fname));
	memcpy(js->JS_parent, j->JIS_info.JP_parent, sizeof(js->JS_parent));
	memcpy(js->JS_documentName, j->JIS_info.JP_documentName,
	       sizeof(js->JS_documentName));
	js->JS_numPages = j->JIS_info.JP_numPages;
	js->JS_time = j->JIS_time;
	js->JS_printing = SJP_NOT_PRINTING;
	/* the first job of a queue is the one printing */
	if (q->QI_curJob == job) {
	    js->JS_printing = SJP_PRINTING;
	    js->JS_totalPage = q->QI_numPhysPgs;
	    js->JS_curPage = q->QI_curPhysPg;
	}
    }
    MemUnlock(mh);
    *ret = mh;
    UnlockQueue();
    return SPOOL_OPERATION_SUCCESSFUL;
}

/* SpoolInfoQueue: a block with the IDs of the jobs in the port's queue;
 * an activity query only says whether there are queues */
static SpoolOpStatus SpoolInfoQueue(SpoolInfoQueueParams *p)
{
    ChunkHandle queue, ch;
    word n = 0, *ids;
    MemHandle mh;

    if (LockQueue()) {
	UnlockQueue();
	return SPOOL_QUEUE_EMPTY;
    }
    if (p->SIQP_activityQuery) {
	UnlockQueue();
	return SPOOL_QUEUE_NOT_EMPTY;
    }
    queue = FindRightQueue(p->SIQP_portInfo);
    if (!queue) {
	UnlockQueue();
	return SPOOL_QUEUE_NOT_FOUND;
    }
    for (ch = QUEUE(queue)->QI_curJob; ch; ch = JOB(ch)->JIS_next)
	n++;
    if (!n) {
	UnlockQueue();
	return SPOOL_QUEUE_EMPTY;
    }
    mh = MemAlloc((word) (n * sizeof(word)), HF_DYNAMIC | HF_SHARABLE,
		  HAF_NO_ERR | HAF_LOCK);
    ids = MemDeref(mh);
    for (ch = QUEUE(queue)->QI_curJob; ch; ch = JOB(ch)->JIS_next)
	*ids++ = JOB(ch)->JIS_jobID;
    MemUnlock(mh);
    p->SIQP_retBlock = mh;
    p->SIQP_retNumJobs = n;
    UnlockQueue();
    return SPOOL_OPERATION_SUCCESSFUL;
}

SpoolOpStatus SpoolInfo(SpoolInfoType infoType, SpoolInfoParams *params)
{
    switch (infoType) {
    case SIT_JOB_INFO:
    case SIT_JOB_PARAMETERS:
	return SpoolInfoJob(infoType, params->SIP_jobParams.SIJP_jobID,
			    &params->SIP_jobParams.SIJP_retBlock);
    case SIT_QUEUE_INFO:
	return SpoolInfoQueue(&params->SIP_queueParams);
    }
    return SPOOL_OPERATION_FAILED;
}

/* Hurry: the job right after the one printing */
static SpoolOpStatus Hurry(ChunkHandle job)
{
    ChunkHandle head = QUEUE(JOB(job)->JIS_queue)->QI_curJob, ch;
    JobInfoStruct *h = JOB(head), *p;

    if (head == job || h->JIS_next == job)
	return SPOOL_OPERATION_FAILED;
    for (ch = head; (p = JOB(ch))->JIS_next != job; ch = p->JIS_next)
	;
    p->JIS_next = JOB(job)->JIS_next;		/* out of its place */
    JOB(job)->JIS_next = h->JIS_next;		/* after the head */
    h->JIS_next = job;
    return SPOOL_OPERATION_SUCCESSFUL;
}

/* Delay: the job at the end of the queue */
static SpoolOpStatus Delay(ChunkHandle job)
{
    ChunkHandle head = QUEUE(JOB(job)->JIS_queue)->QI_curJob, ch;
    JobInfoStruct *p;

    if (!JOB(job)->JIS_next || head == job)
	return SPOOL_OPERATION_FAILED;
    for (ch = head; (p = JOB(ch))->JIS_next != job; ch = p->JIS_next)
	;
    p->JIS_next = JOB(job)->JIS_next;
    for (ch = p->JIS_next; (p = JOB(ch))->JIS_next; ch = p->JIS_next)
	;
    p->JIS_next = job;
    JOB(job)->JIS_next = 0;
    return SPOOL_OPERATION_SUCCESSFUL;
}

/* Priority: of the queue's thread, at most PRIORITY_STANDARD */
static SpoolOpStatus Priority(ChunkHandle job, ThreadPriority prio)
{
    ThreadHandle th = QUEUE(JOB(job)->JIS_queue)->QI_thread;

    if (prio < PRIORITY_STANDARD)
	prio = PRIORITY_STANDARD;
    ThreadModify(th, prio, TMF_BASE_PRIO);
    return SPOOL_OPERATION_SUCCESSFUL;
}

static SpoolOpStatus JobAction(word action, word jobID, ThreadPriority p)
{
    ChunkHandle job;
    SpoolOpStatus st;

    if (LockQueue()) {
	UnlockQueue();
	return SPOOL_QUEUE_EMPTY;
    }
    job = MapJobIDtoHandle(jobID);
    if (!job)
	st = SPOOL_JOB_NOT_FOUND;
    else if (action == SJA_HURRY)
	st = Hurry(job);
    else if (action == SJA_DELAY)
	st = Delay(job);
    else
	st = Priority(job, p);
    UnlockQueue();
    return st;
}

SpoolOpStatus SpoolHurryJob(word jobID)
{
    return JobAction(SJA_HURRY, jobID, 0);
}

SpoolOpStatus SpoolDelayJob(word jobID)
{
    return JobAction(SJA_DELAY, jobID, 0);
}

SpoolOpStatus SpoolModifyPriority(word jobID, ThreadPriority priority)
{
    return JobAction(SJA_PRIORITY, jobID, priority);
}

/* SpoolQueueCheckJobsWantingWarning: TRUE if a job would be saved on a
 * shutdown (the user is warned before detaching) */
Boolean SpoolQueueCheckJobsWantingWarning(void)
{
    ChunkHandle qch, jch;
    Boolean found = FALSE;

    if (LockQueue()) {
	UnlockQueue();
	return FALSE;
    }
    for (qch = PQ()->PQ_firstQueue; qch && !found;
	 qch = QUEUE(qch)->QI_next)
	for (jch = QUEUE(qch)->QI_curJob; jch; jch = JOB(jch)->JIS_next)
	    if (!(JOB(jch)->JIS_info.JP_spoolOpts & SO_SHUTDOWN_ACTION)) {
		found = TRUE;
		break;
	    }
    UnlockQueue();
    return found;
}
